#include "polyinproducer.h"
#include "stockclientencode.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace PolyIn
{
namespace
{
// These are the actual stock CClient quality byte profiles in client.h.
// A test checks these numbers against the pinned stock definitions.
constexpr int Quality[2][2][3] = {
    {{12,22,36},{24,35,73}}, // CT_OPUS64, 64 samples
    {{25,45,82},{47,71,165}} // CT_OPUS, 128 samples
};
constexpr int SampleRate = 48000; // SYSTEM_SAMPLE_RATE_HZ
bool Profile ( const InputSpec& s, int samples, size_t capturedChannels )
{
    // Always validate the active route/channel indices before admitting the
    // configuration; an illegal enum must never produce silent PCM.
    switch ( s.route )
    {
    case InputRoute::Left:
        if ( s.channelA >= capturedChannels ) return false;
        break;
    case InputRoute::Right:
        if ( s.channelB >= capturedChannels ) return false;
        break;
    case InputRoute::Stereo:
    case InputRoute::MonoMix:
        if ( s.channelA >= capturedChannels || s.channelB >= capturedChannels ) return false;
        break;
    default: return false;
    }
    if ( s.source >= MaxSources || ( s.codec != 2 && s.codec != 3 ) ||
         ( samples == 64 && s.codec != 3 ) || ( samples == 128 && s.codec != 2 ) ) return false;
    const bool stereo = s.route == InputRoute::Stereo;
    if ( s.raw ) return s.encodedBytes == samples * ( stereo ? 2 : 1 ) * int ( sizeof ( int16_t ) ) &&
                         s.encodedBytes <= int ( MaxEncodedBytes );
    for ( int bytes : Quality[samples == 128][stereo] )
        if ( s.encodedBytes == bytes ) return true;
    return false;
}
}

SourceProducer::~SourceProducer() { Reset(); }
void SourceProducer::Reset()
{
    for ( auto& s : sources_ ) if ( s.encoder ) opus_custom_encoder_destroy ( s.encoder );
    sources_.clear();
    if ( mode_ ) opus_custom_mode_destroy ( mode_ );
    mode_ = nullptr;
    samples_ = 0;
    capturedChannels_ = 0;
}

bool SourceProducer::Configure ( int samples, size_t capturedChannels, const std::vector<InputSpec>& inputs )
{
    if ( ( samples != 64 && samples != 128 ) || capturedChannels == 0 ||
         capturedChannels > MaxCaptureChannels || inputs.empty() || inputs.size() > MaxSources ) return false;
    std::array<bool, MaxSources> seen{};
    for ( const auto& s : inputs )
    {
        if ( !Profile ( s, samples, capturedChannels ) || seen[s.source] ) return false;
        seen[s.source] = true;
    }
    // The physical stock CChannel is source zero in SourceSlots. A producer
    // configuration without it cannot be admitted by the physical owner.
    if ( !seen[0] ) return false;

    // Preserve the Opus history of unchanged sources across in-session churn.
    // A frame size change, or a changed source profile/route, is an explicit
    // format change and creates a new encoder, as in stock client Init.
    const bool sameMode = mode_ && samples_ == samples;
    OpusCustomMode* newMode = mode_;
    int err = 0;
    if ( !sameMode )
    {
        newMode = opus_custom_mode_create ( SampleRate, samples, &err );
        if ( !newMode || err != OPUS_OK ) return false;
    }
    std::vector<Source> next;
    next.reserve ( inputs.size() );
    std::array<bool, MaxSources> reused{};
    for ( const auto& spec : inputs )
    {
        Source source;
        source.config = spec;
        const auto existing = std::find_if ( sources_.begin(), sources_.end(),
            [&]( const Source& old ) { return old.config.source == spec.source; } );
        if ( sameMode && existing != sources_.end() &&
             existing->config.route == spec.route && existing->config.codec == spec.codec &&
             existing->config.raw == spec.raw && existing->config.encodedBytes == spec.encodedBytes &&
             existing->config.channelA == spec.channelA && existing->config.channelB == spec.channelB )
        {
            source.encoder = existing->encoder;
            reused[spec.source] = true;
        }
        else if ( !spec.raw )
        {
            source.encoder = opus_custom_encoder_create ( newMode, spec.route == InputRoute::Stereo ? 2 : 1, &err );
            if ( !source.encoder || err != OPUS_OK )
            {
                if ( source.encoder ) opus_custom_encoder_destroy ( source.encoder );
                for ( const auto& candidate : next )
                    if ( candidate.encoder && !reused[candidate.config.source] )
                        opus_custom_encoder_destroy ( candidate.encoder );
                if ( !sameMode ) opus_custom_mode_destroy ( newMode );
                return false;  // Original configuration remains completely intact.
            }
            StockClientEncoderDefaults ( source.encoder, samples == 64 );
            StockClientEncoderBitrate ( source.encoder, spec.encodedBytes, samples, SampleRate );
        }
        next.push_back ( source );
    }
    for ( auto& old : sources_ )
        if ( old.encoder && !reused[old.config.source] ) opus_custom_encoder_destroy ( old.encoder );
    if ( !sameMode && mode_ ) opus_custom_mode_destroy ( mode_ );
    sources_.swap ( next );
    mode_ = newMode;
    samples_ = samples;
    capturedChannels_ = capturedChannels;
    return true;
}

bool SourceProducer::Encode ( const int16_t* interleaved, size_t availableSamples, bool muted,
                              uint32_t physicalFrame, uint64_t generation, size_t mtu,
                              std::vector<std::vector<uint8_t>>& datagrams )
{
    datagrams.clear();
    if ( !interleaved || !generation || !samples_ ||
         availableSamples < capturedChannels_ * size_t ( samples_ ) ) return false;
    std::vector<SourceRecord> records;
    records.reserve ( sources_.size() );
    std::array<int16_t, 256> routed{}; // max stereo, 128 frames; no heap PCM
    std::array<int16_t, 256> zeros{};
    for ( auto& source : sources_ )
    {
        const auto& cfg = source.config;
        const bool two = cfg.route == InputRoute::Stereo;
        for ( int i = 0; i < samples_; ++i )
        {
            const size_t base = size_t ( i ) * capturedChannels_;
            switch ( cfg.route )
            {
            case InputRoute::Left:    routed[i] = interleaved[base + cfg.channelA]; break;
            case InputRoute::Right:   routed[i] = interleaved[base + cfg.channelB]; break;
            case InputRoute::MonoMix:
                routed[i] = int16_t ( ( int32_t ( interleaved[base + cfg.channelA] ) +
                                        int32_t ( interleaved[base + cfg.channelB] ) ) / 2 );
                break;
            case InputRoute::Stereo:
                routed[2*i]   = interleaved[base + cfg.channelA];
                routed[2*i+1] = interleaved[base + cfg.channelB];
                break;
            }
        }
        SourceRecord r;
        r.source = cfg.source;
        r.codec = cfg.codec;
        r.channels = two ? 2 : 1;
        r.encoded.resize ( cfg.encodedBytes );
        StockClientEncodeFrame ( source.encoder, cfg.raw, muted, routed.data(), zeros.data(),
                                 samples_, r.encoded.data(), cfg.encodedBytes );
        records.push_back ( std::move ( r ) );
    }
    return PackFrame ( physicalFrame, generation, records, mtu, datagrams );
}
}
