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
    { { 12, 22, 36 }, { 24, 35, 73 } }, // CT_OPUS64, 64 samples
    { { 25, 45, 82 }, { 47, 71, 165 } } // CT_OPUS, 128 samples
};
constexpr int SampleRate = 48000; // SYSTEM_SAMPLE_RATE_HZ
bool          Profile ( const InputSpec& s, int samples, size_t capturedChannels )
{
    // Always validate the active route/channel indices before admitting the
    // configuration; an illegal enum must never produce silent PCM.
    switch ( s.route )
    {
    case InputRoute::Left:
        if ( s.channelA >= capturedChannels )
            return false;
        break;
    case InputRoute::Right:
        if ( s.channelB >= capturedChannels )
            return false;
        break;
    case InputRoute::Stereo:
    case InputRoute::MonoMix:
        if ( s.channelA >= capturedChannels || s.channelB >= capturedChannels )
            return false;
        break;
    default:
        return false;
    }
    if ( s.source >= MaxSources || ( s.codec != 2 && s.codec != 3 ) || ( samples == 64 && s.codec != 3 ) || ( samples == 128 && s.codec != 2 ) )
        return false;
    const bool stereo = s.route == InputRoute::Stereo;
    if ( s.raw )
        return s.encodedBytes == samples * ( stereo ? 2 : 1 ) * int ( sizeof ( int16_t ) ) && s.encodedBytes <= int ( MaxEncodedBytes );
    for ( int bytes : Quality[samples == 128][stereo] )
        if ( s.encodedBytes == bytes )
            return true;
    return false;
}
} // namespace

SourceProducer::~SourceProducer() { Reset(); }
bool ValidateCaptureRoutes ( const std::vector<CaptureRoute>& routes, int physicalInputs, int& captureChannels )
{
    if ( routes.size() < 2 || routes.size() > MaxSources )
        return false;
    std::array<bool, MaxCaptureChannels> used{};
    captureChannels = 2;
    for ( size_t i = 0; i < routes.size(); ++i )
    {
        const auto& route = routes[i];
        const int   a = route.iInputChannel1, b = route.iInputChannel2;
        if ( route.strFaderTag.empty() || route.strFaderTag.size() > 48 || route.iInstrument < 0 || route.iInstrument > 255 || a < 0 ||
             a >= physicalInputs || a >= int ( MaxCaptureChannels ) || b < -1 || b >= physicalInputs || b >= int ( MaxCaptureChannels ) || a == b ||
             used[a] || ( b >= 0 && used[b] ) )
            return false;
        for ( size_t previous = 0; previous < i; ++previous )
            if ( routes[previous].strFaderTag == route.strFaderTag )
                return false;
        used[a] = true;
        if ( b >= 0 )
            used[b] = true;
        captureChannels = std::max ( captureChannels, std::max ( a, b ) + 1 );
    }
    return true;
}

int MatchingProfileBytes ( int samples, bool fromStereo, int bytes, bool toStereo, bool raw )
{
    if ( samples != 64 && samples != 128 )
        return 0;
    if ( raw )
        return bytes == samples * ( fromStereo ? 2 : 1 ) * int ( sizeof ( int16_t ) ) ? samples * ( toStereo ? 2 : 1 ) * int ( sizeof ( int16_t ) )
                                                                                      : 0;
    for ( int quality = 0; quality < 3; ++quality )
        if ( Quality[samples == 128][fromStereo][quality] == bytes )
            return Quality[samples == 128][toStereo][quality];
    return 0;
}

void SourceProducer::Reset()
{
    for ( auto& s : sources_ )
        if ( s.encoder )
            opus_custom_encoder_destroy ( s.encoder );
    sources_.clear();
    if ( mode_ )
        opus_custom_mode_destroy ( mode_ );
    mode_             = nullptr;
    samples_          = 0;
    capturedChannels_ = 0;
}

void SourceProducer::Swap ( SourceProducer& other ) noexcept
{
    std::swap ( samples_, other.samples_ );
    std::swap ( capturedChannels_, other.capturedChannels_ );
    std::swap ( mode_, other.mode_ );
    sources_.swap ( other.sources_ );
}

bool SourceProducer::Configure ( int samples, size_t capturedChannels, const std::vector<InputSpec>& inputs )
{
    if ( ( samples != 64 && samples != 128 ) || capturedChannels == 0 || capturedChannels > MaxCaptureChannels || inputs.empty() ||
         inputs.size() > MaxSources )
        return false;
    std::array<bool, MaxSources> seen{};
    for ( const auto& s : inputs )
    {
        if ( !Profile ( s, samples, capturedChannels ) || seen[s.source] )
            return false;
        seen[s.source] = true;
    }
    // The physical stock CChannel is source zero in SourceSlots. A producer
    // configuration without it cannot be admitted by the physical owner.
    if ( !seen[0] )
        return false;

    // Preserve the Opus history of unchanged sources across in-session churn.
    // A frame size change, or a changed source profile/route, is an explicit
    // format change and creates a new encoder, as in stock client Init.
    const bool      sameMode = mode_ && samples_ == samples;
    OpusCustomMode* newMode  = mode_;
    int             err      = 0;
    if ( !sameMode )
    {
        newMode = opus_custom_mode_create ( SampleRate, samples, &err );
        if ( !newMode || err != OPUS_OK )
            return false;
    }
    std::vector<Source> next;
    next.reserve ( inputs.size() );
    std::array<bool, MaxSources> reused{};
    for ( const auto& spec : inputs )
    {
        Source source;
        source.config = spec;
        const auto existing =
            std::find_if ( sources_.begin(), sources_.end(), [&] ( const Source& old ) { return old.config.source == spec.source; } );
        if ( sameMode && existing != sources_.end() && existing->config.route == spec.route && existing->config.codec == spec.codec &&
             existing->config.raw == spec.raw && existing->config.encodedBytes == spec.encodedBytes && existing->config.channelA == spec.channelA &&
             existing->config.channelB == spec.channelB )
        {
            source.encoder      = existing->encoder;
            reused[spec.source] = true;
        }
        else if ( !spec.raw )
        {
            source.encoder = opus_custom_encoder_create ( newMode, spec.route == InputRoute::Stereo ? 2 : 1, &err );
            if ( !source.encoder || err != OPUS_OK )
            {
                if ( source.encoder )
                    opus_custom_encoder_destroy ( source.encoder );
                for ( const auto& candidate : next )
                    if ( candidate.encoder && !reused[candidate.config.source] )
                        opus_custom_encoder_destroy ( candidate.encoder );
                if ( !sameMode )
                    opus_custom_mode_destroy ( newMode );
                return false; // Original configuration remains completely intact.
            }
            StockClientEncoderDefaults ( source.encoder, samples == 64 );
            StockClientEncoderBitrate ( source.encoder, spec.encodedBytes, samples, SampleRate );
        }
        next.push_back ( source );
    }
    for ( auto& old : sources_ )
        if ( old.encoder && !reused[old.config.source] )
            opus_custom_encoder_destroy ( old.encoder );
    if ( !sameMode && mode_ )
        opus_custom_mode_destroy ( mode_ );
    sources_.swap ( next );
    mode_             = newMode;
    samples_          = samples;
    capturedChannels_ = capturedChannels;
    return true;
}

bool SourceProducer::Encode ( const int16_t*                     interleaved,
                              size_t                             availableSamples,
                              bool                               muted,
                              uint32_t                           physicalFrame,
                              uint64_t                           generation,
                              size_t                             mtu,
                              std::vector<std::vector<uint8_t>>& datagrams )
{
    datagrams.clear();
    if ( !interleaved || !generation || !samples_ || availableSamples < capturedChannels_ * size_t ( samples_ ) )
        return false;
    std::vector<SourceRecord> records;
    records.reserve ( sources_.size() );
    std::array<int16_t, 256> routed{}; // max stereo, 128 frames; no heap PCM
    std::array<int16_t, 256> zeros{};
    for ( auto& source : sources_ )
    {
        const auto& cfg = source.config;
        const bool  two = cfg.route == InputRoute::Stereo;
        for ( int i = 0; i < samples_; ++i )
        {
            const size_t base = size_t ( i ) * capturedChannels_;
            switch ( cfg.route )
            {
            case InputRoute::Left:
                routed[i] = interleaved[base + cfg.channelA];
                break;
            case InputRoute::Right:
                routed[i] = interleaved[base + cfg.channelB];
                break;
            case InputRoute::MonoMix:
                routed[i] = int16_t ( ( int32_t ( interleaved[base + cfg.channelA] ) + int32_t ( interleaved[base + cfg.channelB] ) ) / 2 );
                break;
            case InputRoute::Stereo:
                routed[2 * i]     = interleaved[base + cfg.channelA];
                routed[2 * i + 1] = interleaved[base + cfg.channelB];
                break;
            }
        }
        SourceRecord r;
        r.source   = cfg.source;
        r.codec    = cfg.codec;
        r.channels = two ? 2 : 1;
        r.encoded.resize ( cfg.encodedBytes );
        StockClientEncodeFrame ( source.encoder, cfg.raw, muted, routed.data(), zeros.data(), samples_, r.encoded.data(), cfg.encodedBytes );
        records.push_back ( std::move ( r ) );
    }
    return PackFrame ( physicalFrame, generation, records, mtu, datagrams );
}
// Stage05 callback adapter: the same packet format as PackFrame, assembled into
// fixed-capacity caller storage. Encode() above remains unchanged for non-RT use.
// Each encoded source is <= MaxEncodedBytes; no heap allocation on this path.
bool SourceProducer::EncodeBounded ( const int16_t*                         interleaved,
                                     size_t                                 availableSamples,
                                     bool                                   muted,
                                     uint32_t                               physicalFrame,
                                     uint64_t                               generation,
                                     size_t                                 mtu,
                                     std::array<BoundedPacket, MaxSources>& packets,
                                     size_t&                                packetCount )
{
    packetCount = 0;
    if ( !interleaved || !generation || !samples_ || sources_.empty() || availableSamples < capturedChannels_ * size_t ( samples_ ) ||
         mtu > MaxDatagram || mtu < HeaderBytes + FragmentHeaderBytes + 1 )
        return false;
    auto beginPacket = [&]() -> bool {
        if ( packetCount >= packets.size() )
            return false;
        auto& p = packets[packetCount++];
        std::fill_n ( p.data.begin(), HeaderBytes, uint8_t ( 0 ) );
        p.length  = HeaderBytes;
        p.data[0] = 'P';
        p.data[1] = 'L';
        p.data[2] = 'I';
        p.data[3] = 'N';
        p.data[4] = WireVersion;
        Detail::Write32 ( p.data.data() + 8, physicalFrame );
        Detail::Write64 ( p.data.data() + 12, generation );
        return true;
    };
    auto finishPacket = [&]() {
        auto&          p       = packets[packetCount - 1];
        const uint16_t payload = uint16_t ( p.length - HeaderBytes );
        p.data[20]             = uint8_t ( payload );
        p.data[21]             = uint8_t ( payload >> 8 );
    };
    if ( !beginPacket() )
        return false;
    std::array<int16_t, 256>             routed{};
    std::array<int16_t, 256>             zeros{};
    std::array<uint8_t, MaxEncodedBytes> encoded{};
    for ( auto& source : sources_ )
    {
        const auto& cfg    = source.config;
        const bool  stereo = cfg.route == InputRoute::Stereo;
        for ( int i = 0; i < samples_; ++i )
        {
            const size_t base = size_t ( i ) * capturedChannels_;
            switch ( cfg.route )
            {
            case InputRoute::Left:
                routed[i] = interleaved[base + cfg.channelA];
                break;
            case InputRoute::Right:
                routed[i] = interleaved[base + cfg.channelB];
                break;
            case InputRoute::MonoMix:
                routed[i] = int16_t ( ( int32_t ( interleaved[base + cfg.channelA] ) + int32_t ( interleaved[base + cfg.channelB] ) ) / 2 );
                break;
            case InputRoute::Stereo:
                routed[2 * i]     = interleaved[base + cfg.channelA];
                routed[2 * i + 1] = interleaved[base + cfg.channelB];
                break;
            }
        }
        StockClientEncodeFrame ( source.encoder, cfg.raw, muted, routed.data(), zeros.data(), samples_, encoded.data(), cfg.encodedBytes );
        size_t offset = 0;
        while ( offset < size_t ( cfg.encodedBytes ) )
        {
            auto* p = &packets[packetCount - 1];
            if ( size_t ( p->length ) + FragmentHeaderBytes + 1 > mtu || p->data[6] == MaxSources )
            {
                finishPacket();
                if ( !beginPacket() )
                {
                    packetCount = 0;
                    return false;
                }
                p = &packets[packetCount - 1];
            }
            const size_t n       = std::min ( size_t ( cfg.encodedBytes ) - offset, mtu - size_t ( p->length ) - FragmentHeaderBytes );
            uint8_t*     frag    = p->data.data() + p->length;
            const auto   write16 = [] ( uint8_t* q, uint16_t v ) {
                q[0] = uint8_t ( v );
                q[1] = uint8_t ( v >> 8 );
            };
            write16 ( frag, cfg.source );
            frag[2] = cfg.codec;
            frag[3] = stereo ? 2 : 1;
            write16 ( frag + 4, uint16_t ( cfg.encodedBytes ) );
            write16 ( frag + 6, uint16_t ( offset ) );
            write16 ( frag + 8, uint16_t ( n ) );
            std::memcpy ( frag + FragmentHeaderBytes, encoded.data() + offset, n );
            p->length = uint16_t ( size_t ( p->length ) + FragmentHeaderBytes + n );
            ++p->data[6];
            offset += n;
        }
    }
    finishPacket();
    return true;
}

} // namespace PolyIn
