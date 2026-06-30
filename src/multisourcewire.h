/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#pragma once

#include <array>
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

static_assert ( kHeaderBytes + 2 * kMaxRecordBytes <= kMaxApplicationDatagram,
                "two worst-case source records must fit one conservative UDP datagram" );

inline bool SequenceBefore ( const uint32_t a, const uint32_t b ) { return static_cast<int32_t> ( a - b ) < 0; }

struct SourceDescriptor
{
    uint8_t  key          = 0;
    uint8_t  audioChannels = 1; // 1 or 2
    uint16_t payloadBytes = 0;
    bool     raw          = false;
};

struct RoutingRow
{
    std::string tag;
    uint8_t     icon = 0;
    int         channelOne = -1;
    int         channelTwo = -1; // -1 means mono
};

struct RecordView
{
    uint8_t        key    = 0;
    const uint8_t* data   = nullptr;
    uint16_t       length = 0;
};

struct ParsedFragment
{
    uint8_t  flags         = 0;
    uint16_t generation    = 0;
    uint32_t sequence      = 0;
    uint8_t  fragmentIndex = 0;
    uint8_t  fragmentCount = 0;
    uint8_t  recordCount   = 0;
    std::array<RecordView, kMaxSourceRows> records {};
};

struct Datagram
{
    std::array<uint8_t, kMaxApplicationDatagram> bytes {};
    uint16_t                                      length = 0;
};

class FramePacketizer
{
public:
    // The data referenced by records is copied into the packetizer's fixed storage.
    // Packetize performs no dynamic allocation.
    bool Packetize ( uint16_t                generation,
                     uint32_t                sequence,
                     bool                    raw,
                     const RecordView*       records,
                     size_t                  recordCount,
                     const Datagram*&        datagrams,
                     size_t&                 datagramCount );

private:
    std::array<Datagram, kMaxFragments> packetStorage {};
};

bool ParseFragment ( const uint8_t* data, size_t length, ParsedFragment& parsed );

class SessionIngress
{
public:
    // Configure is called only during negotiation/reconnect, never in the audio callback.
    bool Configure ( uint16_t generation, bool raw, const SourceDescriptor* descriptors, size_t descriptorCount, size_t ringFrames = kDefaultIngressFrames );
    void Reset();

    // A malformed, stale or unconfigured datagram returns false and leaves playout state unchanged.
    bool Put ( const uint8_t* data, size_t length );

    // Copies only references.  `records` must have room for DescriptorCount() entries.
    // Returns true when at least one fragment of `sequence` was received. Missing sources have null data.
    bool Read ( uint32_t sequence, RecordView* records, size_t capacity ) const;

    size_t DescriptorCount() const { return sourceDescriptors.size(); }
    uint16_t Generation() const { return configuredGeneration; }
    bool Raw() const { return rawSession; }
    bool HasHighestSequence() const { return haveHighestSequence; }
    uint32_t HighestSequence() const { return highestSequence; }
    size_t RingFrames() const { return ringSlots.size(); }

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

    int FindDescriptor ( uint8_t key ) const;
    void PrepareSlot ( size_t slotIndex, uint32_t sequence, uint8_t fragmentCount );
    bool IsTooOld ( uint32_t sequence ) const;

    uint16_t                      configuredGeneration = 0;
    bool                          rawSession            = false;
    bool                          haveHighestSequence   = false;
    uint32_t                      highestSequence       = 0;
    std::vector<SourceDescriptor> sourceDescriptors;
    std::vector<Slot>             ringSlots;
    std::vector<RecordMeta>       metadata; // [ring slot][descriptor]
    std::vector<uint8_t>          payloadStorage; // [ring slot][descriptor][max payload]
};

enum class NegotiationState : uint8_t
{
    Legacy,
    CapabilityRequested,
    ConfigurationQueued,
    Prepared,
    Active,
    Refused
};

class Negotiation
{
public:
    void Reset() { state = NegotiationState::Legacy; generation = 0; }
    bool BeginCapabilityRequest();
    bool OnCapabilityResponse ( uint8_t protocolVersion );
    bool OnConfigurationQueued();
    bool OnAccept ( uint16_t acceptedGeneration );
    void OnReject() { state = NegotiationState::Refused; }
    void OnTimeout() { if ( state != NegotiationState::Active ) state = NegotiationState::Legacy; }
    bool OnFirstAcceptedFrame ( uint16_t packetGeneration );

    bool CanSendAdvanced() const { return state == NegotiationState::Prepared || state == NegotiationState::Active; }
    NegotiationState State() const { return state; }
    uint16_t Generation() const { return generation; }

private:
    NegotiationState state      = NegotiationState::Legacy;
    uint16_t         generation = 0;
};

bool ValidateRoutingRows ( const RoutingRow* rows, size_t count, int physicalInputChannels, std::string* error = nullptr );

} // namespace MultiSource
