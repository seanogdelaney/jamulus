/* Deterministic non-Qt tests for MultiSource wire/reassembly/state code. */
#include "../src/multisourcewire.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace MultiSource;

namespace
{
RecordView MakeRecord ( uint8_t key, const std::vector<uint8_t>& payload )
{
    return RecordView { key, payload.data(), static_cast<uint16_t> ( payload.size() ) };
}

void TestRoundTripAndRawFlag()
{
    std::vector<uint8_t> mono ( 100, 0x11 );
    std::vector<uint8_t> stereo ( kMaxRawStereoPayloadBytes, 0x22 );
    const std::array<RecordView, 2> records { MakeRecord ( 1, mono ), MakeRecord ( 2, stereo ) };
    FramePacketizer packetizer;
    const Datagram* datagrams = nullptr;
    size_t datagramCount = 0;
    assert ( packetizer.Packetize ( 9, 0x12345678, true, records.data(), records.size(), datagrams, datagramCount ) );
    assert ( datagramCount == 1 );
    ParsedFragment parsed;
    assert ( ParseFragment ( datagrams[0].bytes.data(), datagrams[0].length, parsed ) );
    assert ( parsed.generation == 9 && parsed.sequence == 0x12345678 && ( parsed.flags & 1 ) != 0 );
    assert ( parsed.recordCount == 2 && parsed.records[0].key == 1 && parsed.records[1].key == 2 );
    assert ( parsed.records[0].length == mono.size() && parsed.records[1].length == stereo.size() );
}

void TestFragmentLossLeavesOtherRecords()
{
    std::array<std::vector<uint8_t>, 4> payloads;
    for ( size_t i = 0; i < payloads.size(); ++i ) payloads[i].assign ( kMaxRawStereoPayloadBytes, static_cast<uint8_t> ( i + 1 ) );
    std::array<RecordView, 4> records;
    std::array<SourceDescriptor, 4> descriptors;
    for ( size_t i = 0; i < records.size(); ++i )
    {
        records[i] = MakeRecord ( static_cast<uint8_t> ( i + 1 ), payloads[i] );
        descriptors[i] = SourceDescriptor { static_cast<uint8_t> ( i + 1 ), 2, static_cast<uint16_t> ( payloads[i].size() ), true };
    }
    FramePacketizer packetizer;
    const Datagram* datagrams = nullptr;
    size_t datagramCount = 0;
    assert ( packetizer.Packetize ( 4, 17, true, records.data(), records.size(), datagrams, datagramCount ) );
    assert ( datagramCount == 2 );
    SessionIngress ingress;
    assert ( ingress.Configure ( 4, true, descriptors.data(), descriptors.size(), 8 ) );
    assert ( ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) );
    std::array<RecordView, 4> read {};
    assert ( ingress.Read ( 17, read.data(), read.size() ) );
    assert ( read[0].data != nullptr && read[1].data != nullptr );
    assert ( read[2].data == nullptr && read[3].data == nullptr );
    assert ( read[0].data[0] == 1 && read[1].data[0] == 2 );
}

void TestMalformedAndDuplicate()
{
    std::vector<uint8_t> payload ( 10, 1 );
    const std::array<RecordView, 1> record { MakeRecord ( 1, payload ) };
    FramePacketizer packetizer;
    const Datagram* datagrams = nullptr;
    size_t count = 0;
    assert ( packetizer.Packetize ( 1, 1, false, record.data(), 1, datagrams, count ) );
    ParsedFragment parsed;
    assert ( !ParseFragment ( datagrams[0].bytes.data(), 4, parsed ) );
    auto corrupt = datagrams[0];
    corrupt.bytes[11] = 0;
    assert ( !ParseFragment ( corrupt.bytes.data(), corrupt.length, parsed ) );

    SourceDescriptor descriptor { 1, 1, 10, false };
    SessionIngress ingress;
    assert ( ingress.Configure ( 1, false, &descriptor, 1, 4 ) );
    assert ( ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) );
    assert ( !ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) ); // duplicate fragment

    corrupt = datagrams[0];
    corrupt.bytes[14] = 2; // unknown key
    assert ( !ingress.Put ( corrupt.bytes.data(), corrupt.length ) );
}

void TestNegotiationFallback()
{
    Negotiation client;
    assert ( client.BeginCapabilityRequest() );
    // Generic ACK has no API/state effect and the timeout leaves legacy audio active.
    client.OnTimeout();
    assert ( client.State() == NegotiationState::Legacy && !client.CanSendAdvanced() );
    assert ( client.BeginCapabilityRequest() );
    assert ( client.OnCapabilityResponse ( kVersion ) );
    assert ( client.OnConfigurationQueued() );
    assert ( client.OnAccept ( 7 ) );
    assert ( client.CanSendAdvanced() );
    assert ( !client.OnFirstAcceptedFrame ( 6 ) );
    assert ( client.OnFirstAcceptedFrame ( 7 ) );
    assert ( client.State() == NegotiationState::Active );
}

void TestRoutingValidation()
{
    const std::array<RoutingRow, 3> valid { RoutingRow { "Vox", 1, 0, -1 }, RoutingRow { "Guitar", 2, 1, -1 },
                                              RoutingRow { "Keys", 3, 2, 3 } };
    assert ( ValidateRoutingRows ( valid.data(), valid.size(), 4 ) );
    auto duplicate = valid;
    duplicate[1].channelOne = 0;
    assert ( !ValidateRoutingRows ( duplicate.data(), duplicate.size(), 4 ) );
}
} // namespace

int main()
{
    TestRoundTripAndRawFlag();
    TestFragmentLossLeavesOtherRecords();
    TestMalformedAndDuplicate();
    TestNegotiationFallback();
    TestRoutingValidation();
    std::cout << "multisourcewire tests: PASS\n";
    return 0;
}
