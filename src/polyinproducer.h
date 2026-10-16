/******************************************************************************\
 * Optional source-specific routing and stock-client codec state.
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#pragma once
#include "polyinwire.h"
#include <cstdint>
#include <vector>
#ifdef USE_OPUS_SHARED_LIB
#    include "opus/opus_custom.h"
#else
#    include "opus_custom.h"
#endif

namespace PolyIn
{
// One existing physical capture callback and clock supply an interleaved PCM
// frame, potentially with N capture channels. Platform capture selection and
// negotiating the extra channels are Stage 5 obligations. No new device, clock
// or socket is owned here.
// Left / Right select channelA / channelB of a caller-selected channel pair.
// The names retain the original stereo routing meaning for pair (0,1).
enum class InputRoute { Left, Right, Stereo, MonoMix };
constexpr size_t MaxCaptureChannels = MaxSources * 2; // up to 20 independent stereo pairs
struct InputSpec
{
    uint16_t source = 0;
    InputRoute route = InputRoute::Left;
    uint8_t codec = 3; // stock CT_OPUS(2) or CT_OPUS64(3)
    bool raw = false;
    int encodedBytes = 0;
    uint16_t channelA = 0;  // index in the SAME shared physical capture frame
    uint16_t channelB = 1;  // only read for Right, Stereo, MonoMix
};
// Stage05 producer-only real-time output. Storage and packet count have fixed
// bounds; no vector/string allocation occurs on the capture callback.
struct BoundedPacket
{
    std::array<uint8_t, MaxDatagram> data{};
    uint16_t length = 0;
};
class SourceProducer
{
public:
    SourceProducer() = default;
    ~SourceProducer();
    SourceProducer ( const SourceProducer& ) = delete;
    SourceProducer& operator= ( const SourceProducer& ) = delete;
    // Use exactly 64 or 128 samples, with stock quality sizes for compressed
    // frames or stock raw frame byte counts. Reconfiguration is explicit only.
    // capturedChannels is the physical frame stride (1..MaxCaptureChannels).
    // Source zero (the physical stock CChannel) is mandatory, consistent with
    // SourceSlots::Reserve. Reject invalid IDs, routes and channel indices.
    bool Configure ( int samples, size_t capturedChannels, const std::vector<InputSpec>& sources );
    void Reset();
    void Swap ( SourceProducer& other ) noexcept;
    // One caller-owned physical frame identity and epoch; one packing pass.
    // The existing physical sender, added in Stage 5, sends resulting datagrams.
    // availableSamples counts interleaved int16_t values, not samples/channel.
    bool Encode ( const int16_t* interleaved, size_t availableSamples, bool muted,
                  uint32_t physicalFrame, uint64_t generation, size_t mtu,
                  std::vector<std::vector<uint8_t>>& datagrams );
    // The earlier flexible Encode/PackFrame interface remains for offline users.
    // Actual sound callbacks MUST use this preallocated packet API instead.
    bool EncodeBounded ( const int16_t* interleaved, size_t availableSamples, bool muted,
                         uint32_t physicalFrame, uint64_t generation, size_t mtu,
                         std::array<BoundedPacket, MaxSources>& packets, size_t& packetCount );
    size_t Count() const { return sources_.size(); }
private:
    struct Source
    {
        InputSpec config;
        OpusCustomEncoder* encoder = nullptr;
    };
    int samples_ = 0;
    size_t capturedChannels_ = 0;
    OpusCustomMode* mode_ = nullptr;
    std::vector<Source> sources_;
};
}
