/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace MultiSource
{
constexpr uint16_t kMagic                    = 0x4d53; // "MS", deliberately non-zero
constexpr uint8_t  kVersion                  = 1;
constexpr size_t   kHeaderBytes              = 14;
constexpr size_t   kRecordHeaderBytes        = 3;
constexpr size_t   kMaxApplicationDatagram   = 1200;
constexpr size_t   kMaxSourceRows            = 64; // MAX_NUM_IN_OUT_CHANNELS in Jamulus
constexpr size_t   kMaxRawStereoPayloadBytes = 2 * 128 * sizeof ( int16_t );
constexpr size_t   kMaxRecordBytes           = kRecordHeaderBytes + kMaxRawStereoPayloadBytes;
constexpr size_t   kMaxFragments             = ( kMaxSourceRows + 1 ) / 2; // 2 worst-case records/datagram
constexpr size_t   kDefaultIngressFrames     = 32;
// Keep Advanced auto mode out of the legacy ten-frame startup default.  The
// ordinary CNetBufWithStats simulation also treats two frames as its lowest
// practical candidate; one frame has no useful sample-rate-offset margin.
constexpr int kMinAutoIngressFrames = 2;
constexpr int kMaxAutoIngressFrames = 20;

static_assert ( kHeaderBytes + 2 * kMaxRecordBytes <= kMaxApplicationDatagram,
                "two worst-case source records must fit one conservative UDP datagram" );

inline bool SequenceBefore ( const uint32_t a, const uint32_t b ) { return static_cast<int32_t> ( a - b ) < 0; }

// One server callback covers either 64 or 128 PCM samples.  A 128-sample
// server must emit two independently encoded 64-sample return packets for an
// OPUS64 client; otherwise the return stream is delivered at half cadence.
constexpr int ReturnPacketsPerServerTick ( const bool serverUsesDoubleSystemFrameSize, const bool returnUses64SampleFrames )
{
    return serverUsesDoubleSystemFrameSize && returnUses64SampleFrames ? 2 : 1;
}

// Return the newest not-yet-played sequence which preserves `targetFrames`
// logical frames behind the newest arrival.  It only moves forward, avoiding
// replay when a target is reduced after a backlog has accumulated.
uint32_t ReanchorPlayoutSequence ( uint32_t currentNextSequence, uint32_t highestReceivedSequence, int targetFrames );

struct SourceDescriptor
{
    SourceDescriptor() = default;
    SourceDescriptor ( const uint8_t sourceKey, const uint8_t channels, const uint16_t bytes, const bool isRaw ) :
        key ( sourceKey ),
        audioChannels ( channels ),
        payloadBytes ( bytes ),
        raw ( isRaw )
    {}

    uint8_t  key           = 0;
    uint8_t  audioChannels = 1; // 1 or 2
    uint16_t payloadBytes  = 0;
    bool     raw           = false;
};

struct RoutingRow
{
    RoutingRow() = default;
    RoutingRow ( const std::string& sourceTag, const uint8_t sourceIcon, const int firstChannel, const int secondChannel ) :
        tag ( sourceTag ),
        icon ( sourceIcon ),
        channelOne ( firstChannel ),
        channelTwo ( secondChannel )
    {}

    std::string tag;
    uint8_t     icon       = 0;
    int         channelOne = -1;
    int         channelTwo = -1; // -1 means mono
};

struct RecordView
{
    RecordView() = default;
    RecordView ( const uint8_t sourceKey, const uint8_t* const payload, const uint16_t payloadLength ) :
        key ( sourceKey ),
        data ( payload ),
        length ( payloadLength )
    {}

    uint8_t        key    = 0;
    const uint8_t* data   = nullptr;
    uint16_t       length = 0;
};

struct ParsedFragment
{
    uint8_t                                flags         = 0;
    uint16_t                               generation    = 0;
    uint32_t                               sequence      = 0;
    uint8_t                                fragmentIndex = 0;
    uint8_t                                fragmentCount = 0;
    uint8_t                                recordCount   = 0;
    std::array<RecordView, kMaxSourceRows> records{};
};

struct Datagram
{
    std::array<uint8_t, kMaxApplicationDatagram> bytes{};
    uint16_t                                     length = 0;
};

class FramePacketizer
{
public:
    // The data referenced by records is copied into the packetizer's fixed storage.
    // Packetize performs no dynamic allocation.
    bool Packetize ( uint16_t          generation,
                     uint32_t          sequence,
                     bool              raw,
                     const RecordView* records,
                     size_t            recordCount,
                     const Datagram*&  datagrams,
                     size_t&           datagramCount );

private:
    std::array<Datagram, kMaxFragments> packetStorage{};
};

bool ParseFragment ( const uint8_t* data, size_t length, ParsedFragment& parsed );

class SessionIngress
{
public:
    // Configure is called only during negotiation/reconnect, never in the audio callback.
    bool Configure ( uint16_t                generation,
                     bool                    raw,
                     const SourceDescriptor* descriptors,
                     size_t                  descriptorCount,
                     size_t                  ringFrames = kDefaultIngressFrames );
    void Reset();

    // A malformed, stale or unconfigured datagram returns false and leaves playout state unchanged.
    // `firstFragmentForSequence`, when supplied, is set only for the first
    // valid fragment accepted for one logical session frame.  This lets the
    // session-level jitter estimator count one timing observation per frame,
    // rather than one per UDP fragment.
    bool Put ( const uint8_t* data, size_t length, bool* firstFragmentForSequence = nullptr );

    // Feed the shared ingress auto-jitter estimator.  Arrival is called once
    // for a logical frame; playout is called once for every consumed sequence.
    // Fragment/source loss deliberately does not count as a session timing
    // failure when another fragment for the frame arrived.
    void ObserveArrival ( uint32_t sequence, uint32_t nextPlayoutSequence, int currentTargetFrames );
    void ObservePlayoutResult ( bool frameReceived, int currentTargetFrames );
    int  AutoTargetFrames() const;

    // Copies only references.  `records` must have room for DescriptorCount() entries.
    // Returns true when at least one fragment of `sequence` was received. Missing sources have null data.
    bool Read ( uint32_t sequence, RecordView* records, size_t capacity ) const;

    size_t   DescriptorCount() const { return sourceDescriptors.size(); }
    uint16_t Generation() const { return configuredGeneration; }
    bool     Raw() const { return rawSession; }
    bool     HasHighestSequence() const { return haveHighestSequence; }
    uint32_t HighestSequence() const { return highestSequence; }
    size_t   RingFrames() const { return ringSlots.size(); }

private:
    struct Slot
    {
        bool     occupied      = false;
        uint32_t sequence      = 0;
        uint32_t fragmentMask  = 0;
        uint8_t  fragmentCount = 0;
    };

    struct RecordMeta
    {
        bool     present = false;
        uint16_t length  = 0;
    };

    int  FindDescriptor ( uint8_t key ) const;
    void PrepareSlot ( size_t slotIndex, uint32_t sequence, uint8_t fragmentCount );
    bool IsTooOld ( uint32_t sequence ) const;
    void UpdateAutoStatistic ( int candidateFrames, bool late );

    uint16_t                      configuredGeneration = 0;
    bool                          rawSession           = false;
    bool                          haveHighestSequence  = false;
    uint32_t                      highestSequence      = 0;
    std::vector<SourceDescriptor> sourceDescriptors;
    std::vector<Slot>             ringSlots;
    std::vector<RecordMeta>       metadata;       // [ring slot][descriptor]
    std::vector<uint8_t>          payloadStorage; // [ring slot][descriptor][max payload]
    // Exponentially decayed late-arrival rate, scaled by 10000.  This is
    // fixed-size state; no source or fragment creates another jitter buffer.
    std::array<uint16_t, kMaxAutoIngressFrames + 1> autoLateScores{};
};

// The client intentionally advances only after each prerequisite has been
// observed.  "Queued" means present in CProtocol's ACK-gated FIFO; "Awaiting"
// means the first physical datagram for that logical request has actually been
// handed to the socket.  This distinction prevents startup traffic from
// consuming the Advanced timeout before the request itself has left the queue.
enum class NegotiationState : uint8_t
{
    Legacy,
    AwaitingSplitCapability,
    CapabilityQueued,
    AwaitingCapabilityResponse,
    ConfigurationQueued,
    AwaitingConfigurationResponse,
    Prepared,
    AwaitingActivation,
    Active,
    Refused
};

class Negotiation
{
public:
    void Reset();
    bool Begin();
    bool OnSplitCapabilityReceived();
    bool OnCapabilityRequestSent();
    bool OnCapabilityResponse ( uint8_t protocolVersion );
    bool OnConfigurationRequestSent();
    bool OnAccept ( uint16_t acceptedGeneration );
    void OnReject();
    void OnTimeout();
    bool OnFirstAcceptedFrame ( uint16_t packetGeneration );
    bool OnActivation ( uint16_t activatedGeneration );

    bool             CanSendAdvanced() const;
    bool             IsPreAcceptance() const;
    bool             IsAwaitingActivation() const;
    NegotiationState State() const { return state.load ( std::memory_order_acquire ); }
    uint16_t         Generation() const { return generation.load ( std::memory_order_acquire ); }

private:
    bool Transition ( NegotiationState expected, NegotiationState desired );

    std::atomic<NegotiationState> state{ NegotiationState::Legacy };
    std::atomic<uint16_t>         generation{ 0 };
};

bool ValidateRoutingRows ( const RoutingRow* rows, size_t count, int physicalInputChannels, std::string* error = nullptr );

} // namespace MultiSource
