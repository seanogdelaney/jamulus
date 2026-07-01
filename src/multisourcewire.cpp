/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#include "multisourcewire.h"

#include <algorithm>
#include <cstring>

namespace MultiSource
{
namespace
{
void Put16 ( uint8_t* target, const uint16_t value )
{
    target[0] = static_cast<uint8_t> ( value >> 8 );
    target[1] = static_cast<uint8_t> ( value & 0xff );
}

void Put32 ( uint8_t* target, const uint32_t value )
{
    target[0] = static_cast<uint8_t> ( value >> 24 );
    target[1] = static_cast<uint8_t> ( ( value >> 16 ) & 0xff );
    target[2] = static_cast<uint8_t> ( ( value >> 8 ) & 0xff );
    target[3] = static_cast<uint8_t> ( value & 0xff );
}

uint16_t Get16 ( const uint8_t* source ) { return static_cast<uint16_t> ( ( source[0] << 8 ) | source[1] ); }
uint32_t Get32 ( const uint8_t* source )
{
    return ( static_cast<uint32_t> ( source[0] ) << 24 ) | ( static_cast<uint32_t> ( source[1] ) << 16 ) |
           ( static_cast<uint32_t> ( source[2] ) << 8 ) | source[3];
}

bool IsDuplicateKey ( const RecordView* records, const size_t count, const uint8_t key )
{
    for ( size_t i = 0; i < count; ++i )
    {
        if ( records[i].key == key )
        {
            return true;
        }
    }
    return false;
}

constexpr uint32_t kAutoScoreScale = 10000;
constexpr uint32_t kAutoScoreDecay = 63;
// 0.05 %. One late logical frame immediately raises the target, while clean
// frame arrivals decay the event in roughly 0.6 s at 128 samples / 48 kHz.
constexpr uint32_t kAutoScoreLimit = 5;
} // namespace

uint32_t ReanchorPlayoutSequence ( const uint32_t currentNextSequence, const uint32_t highestReceivedSequence, const int targetFrames )
{
    if ( targetFrames <= 1 )
    {
        return SequenceBefore ( currentNextSequence, highestReceivedSequence ) ? highestReceivedSequence : currentNextSequence;
    }
    const uint32_t desired = highestReceivedSequence - static_cast<uint32_t> ( targetFrames - 1 );
    return SequenceBefore ( currentNextSequence, desired ) ? desired : currentNextSequence;
}

bool FramePacketizer::Packetize ( const uint16_t          generation,
                                  const uint32_t          sequence,
                                  const bool              raw,
                                  const RecordView* const records,
                                  const size_t            recordCount,
                                  const Datagram*&        datagrams,
                                  size_t&                 datagramCount )
{
    datagrams     = nullptr;
    datagramCount = 0;
    if ( ( records == nullptr ) || ( recordCount == 0 ) || ( recordCount > kMaxSourceRows ) )
    {
        return false;
    }

    std::array<size_t, kMaxFragments> firstRecord{};
    std::array<size_t, kMaxFragments> recordsInPacket{};
    size_t                            packetCount = 0;
    size_t                            recordIndex = 0;

    while ( recordIndex < recordCount )
    {
        if ( packetCount >= kMaxFragments )
        {
            return false;
        }
        firstRecord[packetCount] = recordIndex;
        size_t used              = kHeaderBytes;
        while ( recordIndex < recordCount )
        {
            const RecordView& record = records[recordIndex];
            if ( ( record.data == nullptr ) || ( record.length == 0 ) || ( record.length > kMaxRawStereoPayloadBytes ) ||
                 IsDuplicateKey ( records + firstRecord[packetCount], recordIndex - firstRecord[packetCount], record.key ) )
            {
                return false;
            }
            const size_t required = kRecordHeaderBytes + record.length;
            if ( used + required > kMaxApplicationDatagram )
            {
                break;
            }
            used += required;
            ++recordIndex;
            ++recordsInPacket[packetCount];
        }
        if ( recordsInPacket[packetCount] == 0 )
        {
            return false;
        }
        ++packetCount;
    }

    for ( size_t packet = 0; packet < packetCount; ++packet )
    {
        Datagram& datagram = packetStorage[packet];
        uint8_t*  out      = datagram.bytes.data();
        Put16 ( out, kMagic );
        out[2] = kVersion;
        out[3] = raw ? 1 : 0;
        Put16 ( out + 4, generation );
        Put32 ( out + 6, sequence );
        out[10]         = static_cast<uint8_t> ( packet );
        out[11]         = static_cast<uint8_t> ( packetCount );
        out[12]         = static_cast<uint8_t> ( recordsInPacket[packet] );
        out[13]         = 0;
        size_t position = kHeaderBytes;
        for ( size_t i = 0; i < recordsInPacket[packet]; ++i )
        {
            const RecordView& record = records[firstRecord[packet] + i];
            out[position++]          = record.key;
            Put16 ( out + position, record.length );
            position += 2;
            std::memcpy ( out + position, record.data, record.length );
            position += record.length;
        }
        datagram.length = static_cast<uint16_t> ( position );
    }

    datagrams     = packetStorage.data();
    datagramCount = packetCount;
    return true;
}

bool ParseFragment ( const uint8_t* const data, const size_t length, ParsedFragment& parsed )
{
    parsed = ParsedFragment{};
    if ( ( data == nullptr ) || ( length < kHeaderBytes ) || ( length > kMaxApplicationDatagram ) || ( Get16 ( data ) != kMagic ) ||
         ( data[2] != kVersion ) || ( ( data[3] & ~uint8_t ( 1 ) ) != 0 ) || ( data[13] != 0 ) )
    {
        return false;
    }

    parsed.flags         = data[3];
    parsed.generation    = Get16 ( data + 4 );
    parsed.sequence      = Get32 ( data + 6 );
    parsed.fragmentIndex = data[10];
    parsed.fragmentCount = data[11];
    parsed.recordCount   = data[12];
    if ( ( parsed.fragmentCount == 0 ) || ( parsed.fragmentCount > kMaxFragments ) || ( parsed.fragmentIndex >= parsed.fragmentCount ) ||
         ( parsed.recordCount == 0 ) || ( parsed.recordCount > kMaxSourceRows ) )
    {
        return false;
    }

    size_t position = kHeaderBytes;
    for ( uint8_t i = 0; i < parsed.recordCount; ++i )
    {
        if ( position + kRecordHeaderBytes > length )
        {
            return false;
        }
        RecordView& record = parsed.records[i];
        record.key         = data[position++];
        record.length      = Get16 ( data + position );
        position += 2;
        if ( ( record.length == 0 ) || ( record.length > kMaxRawStereoPayloadBytes ) || ( position + record.length > length ) ||
             IsDuplicateKey ( parsed.records.data(), i, record.key ) )
        {
            return false;
        }
        record.data = data + position;
        position += record.length;
    }
    return position == length;
}

bool SessionIngress::Configure ( const uint16_t                generation,
                                 const bool                    raw,
                                 const SourceDescriptor* const descriptors,
                                 const size_t                  descriptorCount,
                                 const size_t                  ringFrames )
{
    Reset();
    if ( ( descriptors == nullptr ) || ( descriptorCount == 0 ) || ( descriptorCount > kMaxSourceRows ) || ( ringFrames == 0 ) )
    {
        return false;
    }
    for ( size_t i = 0; i < descriptorCount; ++i )
    {
        if ( ( descriptors[i].key == 0 ) || ( descriptors[i].audioChannels < 1 ) || ( descriptors[i].audioChannels > 2 ) ||
             ( descriptors[i].payloadBytes == 0 ) || ( descriptors[i].payloadBytes > kMaxRawStereoPayloadBytes ) || ( descriptors[i].raw != raw ) ||
             FindDescriptor ( descriptors[i].key ) >= 0 )
        {
            Reset();
            return false;
        }
        sourceDescriptors.push_back ( descriptors[i] );
    }

    configuredGeneration = generation;
    rawSession           = raw;
    ringSlots.resize ( ringFrames );
    metadata.resize ( ringFrames * descriptorCount );
    payloadStorage.resize ( ringFrames * descriptorCount * kMaxRawStereoPayloadBytes );
    return true;
}

void SessionIngress::Reset()
{
    configuredGeneration = 0;
    rawSession           = false;
    haveHighestSequence  = false;
    highestSequence      = 0;
    sourceDescriptors.clear();
    ringSlots.clear();
    metadata.clear();
    payloadStorage.clear();
    autoLateScores.fill ( 0 );
}

int SessionIngress::FindDescriptor ( const uint8_t key ) const
{
    for ( size_t i = 0; i < sourceDescriptors.size(); ++i )
    {
        if ( sourceDescriptors[i].key == key )
        {
            return static_cast<int> ( i );
        }
    }
    return -1;
}

void SessionIngress::PrepareSlot ( const size_t slotIndex, const uint32_t sequence, const uint8_t fragmentCount )
{
    Slot& slot          = ringSlots[slotIndex];
    slot.occupied       = true;
    slot.sequence       = sequence;
    slot.fragmentMask   = 0;
    slot.fragmentCount  = fragmentCount;
    const size_t offset = slotIndex * sourceDescriptors.size();
    std::fill ( metadata.begin() + static_cast<std::ptrdiff_t> ( offset ),
                metadata.begin() + static_cast<std::ptrdiff_t> ( offset + sourceDescriptors.size() ),
                RecordMeta{} );
}

bool SessionIngress::IsTooOld ( const uint32_t sequence ) const
{
    return haveHighestSequence && SequenceBefore ( sequence, highestSequence ) &&
           static_cast<uint32_t> ( highestSequence - sequence ) >= ringSlots.size();
}

bool SessionIngress::Put ( const uint8_t* const data, const size_t length, bool* const firstFragmentForSequence )
{
    if ( firstFragmentForSequence != nullptr )
        *firstFragmentForSequence = false;
    ParsedFragment fragment;
    if ( !ParseFragment ( data, length, fragment ) || ( fragment.generation != configuredGeneration ) ||
         ( ( fragment.flags & 1 ) != ( rawSession ? 1 : 0 ) ) || ringSlots.empty() || IsTooOld ( fragment.sequence ) )
    {
        return false;
    }

    std::array<int, kMaxSourceRows> indexes{};
    for ( uint8_t i = 0; i < fragment.recordCount; ++i )
    {
        indexes[i] = FindDescriptor ( fragment.records[i].key );
        if ( ( indexes[i] < 0 ) || ( fragment.records[i].length != sourceDescriptors[static_cast<size_t> ( indexes[i] )].payloadBytes ) )
        {
            return false;
        }
    }

    const size_t slotIndex = fragment.sequence % ringSlots.size();
    Slot&        slot      = ringSlots[slotIndex];
    if ( !slot.occupied || ( slot.sequence != fragment.sequence ) )
    {
        PrepareSlot ( slotIndex, fragment.sequence, fragment.fragmentCount );
        slot = ringSlots[slotIndex];
    }
    const bool firstFragment = slot.fragmentMask == 0;
    if ( slot.fragmentCount != fragment.fragmentCount || ( slot.fragmentMask & ( uint32_t ( 1 ) << fragment.fragmentIndex ) ) != 0 )
    {
        return false;
    }

    const size_t metadataBase = slotIndex * sourceDescriptors.size();
    for ( uint8_t i = 0; i < fragment.recordCount; ++i )
    {
        if ( metadata[metadataBase + static_cast<size_t> ( indexes[i] )].present )
        {
            return false;
        }
    }

    for ( uint8_t i = 0; i < fragment.recordCount; ++i )
    {
        const size_t sourceIndex = static_cast<size_t> ( indexes[i] );
        RecordMeta&  recordMeta  = metadata[metadataBase + sourceIndex];
        recordMeta.present       = true;
        recordMeta.length        = fragment.records[i].length;
        uint8_t* target          = payloadStorage.data() + ( ( slotIndex * sourceDescriptors.size() + sourceIndex ) * kMaxRawStereoPayloadBytes );
        std::memcpy ( target, fragment.records[i].data, fragment.records[i].length );
    }
    slot.fragmentMask |= uint32_t ( 1 ) << fragment.fragmentIndex;
    if ( !haveHighestSequence || SequenceBefore ( highestSequence, fragment.sequence ) )
    {
        haveHighestSequence = true;
        highestSequence     = fragment.sequence;
    }
    if ( firstFragmentForSequence != nullptr )
        *firstFragmentForSequence = firstFragment;
    return true;
}

void SessionIngress::UpdateAutoStatistic ( const int candidateFrames, const bool late )
{
    if ( candidateFrames < kMinAutoIngressFrames || candidateFrames > kMaxAutoIngressFrames )
        return;
    uint32_t score                                          = autoLateScores[static_cast<size_t> ( candidateFrames )];
    score                                                   = ( score * kAutoScoreDecay + ( late ? kAutoScoreScale : 0 ) ) / ( kAutoScoreDecay + 1 );
    autoLateScores[static_cast<size_t> ( candidateFrames )] = static_cast<uint16_t> ( score );
}

void SessionIngress::ObserveArrival ( const uint32_t sequence, const uint32_t nextPlayoutSequence, const int currentTargetFrames )
{
    const int     target = std::max ( kMinAutoIngressFrames, std::min ( kMaxAutoIngressFrames, currentTargetFrames ) );
    const int32_t lead   = static_cast<int32_t> ( sequence - nextPlayoutSequence );
    for ( int candidate = kMinAutoIngressFrames; candidate <= target; ++candidate )
    {
        // With a smaller candidate buffer, playout would already have advanced
        // by target-candidate frames at this wall-clock instant.
        UpdateAutoStatistic ( candidate, lead < target - candidate );
    }
}

void SessionIngress::ObservePlayoutResult ( const bool frameReceived, const int currentTargetFrames )
{
    if ( frameReceived )
        return;
    const int target = std::max ( kMinAutoIngressFrames, std::min ( kMaxAutoIngressFrames, currentTargetFrames ) );
    // A missing logical frame at the current deadline proves that every
    // smaller/equal target would also have missed it.  Do not charge larger
    // candidates: they may still receive a late fragment.
    for ( int candidate = kMinAutoIngressFrames; candidate <= target; ++candidate )
    {
        UpdateAutoStatistic ( candidate, true );
    }
}

int SessionIngress::AutoTargetFrames() const
{
    for ( int candidate = kMinAutoIngressFrames; candidate <= kMaxAutoIngressFrames; ++candidate )
    {
        if ( autoLateScores[static_cast<size_t> ( candidate )] <= kAutoScoreLimit )
            return candidate;
    }
    return kMaxAutoIngressFrames;
}

bool SessionIngress::Read ( const uint32_t sequence, RecordView* const records, const size_t capacity ) const
{
    if ( ( records == nullptr ) || ( capacity < sourceDescriptors.size() ) || ringSlots.empty() )
    {
        return false;
    }
    for ( size_t i = 0; i < sourceDescriptors.size(); ++i )
    {
        records[i] = RecordView{ sourceDescriptors[i].key, nullptr, 0 };
    }
    const Slot& slot = ringSlots[sequence % ringSlots.size()];
    if ( !slot.occupied || ( slot.sequence != sequence ) )
    {
        return false;
    }
    const size_t metadataBase = ( sequence % ringSlots.size() ) * sourceDescriptors.size();
    for ( size_t i = 0; i < sourceDescriptors.size(); ++i )
    {
        const RecordMeta& recordMeta = metadata[metadataBase + i];
        if ( recordMeta.present )
        {
            records[i].length = recordMeta.length;
            records[i].data =
                payloadStorage.data() + ( ( ( sequence % ringSlots.size() ) * sourceDescriptors.size() + i ) * kMaxRawStereoPayloadBytes );
        }
    }
    return true;
}

void Negotiation::Reset()
{
    generation.store ( 0, std::memory_order_release );
    state.store ( NegotiationState::Legacy, std::memory_order_release );
}

bool Negotiation::Transition ( const NegotiationState expected, const NegotiationState desired )
{
    NegotiationState current = expected;
    return state.compare_exchange_strong ( current, desired, std::memory_order_acq_rel, std::memory_order_acquire );
}

bool Negotiation::Begin()
{
    NegotiationState current = state.load ( std::memory_order_acquire );
    while ( current == NegotiationState::Legacy || current == NegotiationState::Refused )
    {
        if ( state.compare_exchange_weak ( current,
                                           NegotiationState::AwaitingSplitCapability,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire ) )
        {
            generation.store ( 0, std::memory_order_release );
            return true;
        }
    }
    return false;
}

bool Negotiation::OnSplitCapabilityReceived() { return Transition ( NegotiationState::AwaitingSplitCapability, NegotiationState::CapabilityQueued ); }

bool Negotiation::OnCapabilityRequestSent()
{
    return Transition ( NegotiationState::CapabilityQueued, NegotiationState::AwaitingCapabilityResponse );
}

bool Negotiation::OnCapabilityResponse ( const uint8_t protocolVersion )
{
    if ( protocolVersion != kVersion )
        return false;
    return Transition ( NegotiationState::AwaitingCapabilityResponse, NegotiationState::ConfigurationQueued );
}

bool Negotiation::OnConfigurationRequestSent()
{
    return Transition ( NegotiationState::ConfigurationQueued, NegotiationState::AwaitingConfigurationResponse );
}

bool Negotiation::OnAccept ( const uint16_t acceptedGeneration )
{
    if ( acceptedGeneration == 0 || State() != NegotiationState::AwaitingConfigurationResponse )
        return false;

    // Publish the generation before making the sendable state visible to the
    // audio callback.  It must never packetize generation zero.
    generation.store ( acceptedGeneration, std::memory_order_release );
    if ( Transition ( NegotiationState::AwaitingConfigurationResponse, NegotiationState::Prepared ) )
        return true;

    generation.store ( 0, std::memory_order_release );
    return false;
}

void Negotiation::OnReject() { state.store ( NegotiationState::Refused, std::memory_order_release ); }

void Negotiation::OnTimeout()
{
    const NegotiationState current = State();
    if ( current != NegotiationState::Active && current != NegotiationState::AwaitingActivation )
    {
        generation.store ( 0, std::memory_order_release );
        state.store ( NegotiationState::Legacy, std::memory_order_release );
    }
}

bool Negotiation::OnFirstAcceptedFrame ( const uint16_t packetGeneration )
{
    if ( packetGeneration != Generation() )
        return false;
    return Transition ( NegotiationState::Prepared, NegotiationState::AwaitingActivation );
}

bool Negotiation::OnActivation ( const uint16_t activatedGeneration )
{
    if ( activatedGeneration == 0 || activatedGeneration != Generation() )
        return false;

    // UDP can deliver the server's confirmation almost immediately after the
    // packet is sent.  Accept either state so a queued Qt protocol event cannot
    // race a just-returning audio callback.
    if ( Transition ( NegotiationState::AwaitingActivation, NegotiationState::Active ) )
        return true;
    return Transition ( NegotiationState::Prepared, NegotiationState::Active );
}

bool Negotiation::CanSendAdvanced() const
{
    const NegotiationState current = State();
    return current == NegotiationState::Prepared || current == NegotiationState::AwaitingActivation || current == NegotiationState::Active;
}

bool Negotiation::IsPreAcceptance() const
{
    const NegotiationState current = State();
    return current == NegotiationState::AwaitingSplitCapability || current == NegotiationState::CapabilityQueued ||
           current == NegotiationState::AwaitingCapabilityResponse || current == NegotiationState::ConfigurationQueued ||
           current == NegotiationState::AwaitingConfigurationResponse;
}

bool Negotiation::IsAwaitingActivation() const { return State() == NegotiationState::AwaitingActivation; }

bool ValidateRoutingRows ( const RoutingRow* const rows, const size_t count, const int physicalInputChannels, std::string* const error )
{
    if ( ( rows == nullptr ) || ( count == 0 ) || ( count > kMaxSourceRows ) || ( physicalInputChannels <= 0 ) )
    {
        if ( error != nullptr )
            *error = "source count or capture channel count is invalid";
        return false;
    }
    for ( size_t i = 0; i < count; ++i )
    {
        if ( rows[i].tag.empty() || rows[i].channelOne < 0 || rows[i].channelOne >= physicalInputChannels ||
             ( rows[i].channelTwo >= physicalInputChannels ) || rows[i].channelTwo == rows[i].channelOne )
        {
            if ( error != nullptr )
                *error = "a source has an empty tag or invalid channel assignment";
            return false;
        }
        for ( size_t earlier = 0; earlier < i; ++earlier )
        {
            if ( rows[earlier].tag == rows[i].tag || rows[earlier].channelOne == rows[i].channelOne ||
                 rows[earlier].channelOne == rows[i].channelTwo ||
                 ( rows[earlier].channelTwo >= 0 &&
                   ( rows[earlier].channelTwo == rows[i].channelOne || rows[earlier].channelTwo == rows[i].channelTwo ) ) )
            {
                if ( error != nullptr )
                    *error = "fader tags and physical input assignments must be unique";
                return false;
            }
        }
    }
    return true;
}

} // namespace MultiSource
