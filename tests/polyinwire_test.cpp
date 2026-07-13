/* Deterministic non-Qt tests for Poly-in wire/reassembly/state code. */
#include "../src/polyinwire.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace PolyIn;

namespace
{
RecordView MakeRecord ( uint8_t key, const std::vector<uint8_t>& payload )
{
    return RecordView{ key, payload.data(), static_cast<uint16_t> ( payload.size() ) };
}

void TestRoundTripAndRawFlag()
{
    std::vector<uint8_t>            mono ( 100, 0x11 );
    std::vector<uint8_t>            stereo ( kMaxRawStereoPayloadBytes, 0x22 );
    const std::array<RecordView, 2> records{ MakeRecord ( 1, mono ), MakeRecord ( 2, stereo ) };
    FramePacketizer                 packetizer;
    const Datagram*                 datagrams     = nullptr;
    size_t                          datagramCount = 0;
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
    for ( size_t i = 0; i < payloads.size(); ++i )
        payloads[i].assign ( kMaxRawStereoPayloadBytes, static_cast<uint8_t> ( i + 1 ) );
    std::array<RecordView, 4>       records;
    std::array<SourceDescriptor, 4> descriptors;
    for ( size_t i = 0; i < records.size(); ++i )
    {
        records[i]     = MakeRecord ( static_cast<uint8_t> ( i + 1 ), payloads[i] );
        descriptors[i] = SourceDescriptor{ static_cast<uint8_t> ( i + 1 ), 2, static_cast<uint16_t> ( payloads[i].size() ), true };
    }
    FramePacketizer packetizer;
    const Datagram* datagrams     = nullptr;
    size_t          datagramCount = 0;
    assert ( packetizer.Packetize ( 4, 17, true, records.data(), records.size(), datagrams, datagramCount ) );
    assert ( datagramCount == 2 );
    SessionIngress ingress;
    assert ( ingress.Configure ( 4, true, descriptors.data(), descriptors.size(), 8 ) );
    assert ( ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) );
    std::array<RecordView, 4> read{};
    assert ( ingress.Read ( 17, read.data(), read.size() ) );
    assert ( read[0].data != nullptr && read[1].data != nullptr );
    assert ( read[2].data == nullptr && read[3].data == nullptr );
    assert ( read[0].data[0] == 1 && read[1].data[0] == 2 );
}

void TestMalformedAndDuplicate()
{
    std::vector<uint8_t>            payload ( 10, 1 );
    const std::array<RecordView, 1> record{ MakeRecord ( 1, payload ) };
    FramePacketizer                 packetizer;
    const Datagram*                 datagrams = nullptr;
    size_t                          count     = 0;
    assert ( packetizer.Packetize ( 1, 1, false, record.data(), 1, datagrams, count ) );
    ParsedFragment parsed;
    assert ( !ParseFragment ( datagrams[0].bytes.data(), 4, parsed ) );
    auto corrupt      = datagrams[0];
    corrupt.bytes[11] = 0;
    assert ( !ParseFragment ( corrupt.bytes.data(), corrupt.length, parsed ) );

    SourceDescriptor descriptor{ 1, 1, 10, false };
    SessionIngress   ingress;
    assert ( ingress.Configure ( 1, false, &descriptor, 1, 4 ) );
    assert ( ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) );
    assert ( !ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) ); // duplicate fragment

    corrupt           = datagrams[0];
    corrupt.bytes[14] = 2; // unknown key
    assert ( !ingress.Put ( corrupt.bytes.data(), corrupt.length ) );
}

void TestNegotiationFallback()
{
    Negotiation client;
    assert ( client.Begin() );
    assert ( client.State() == NegotiationState::AwaitingSplitCapability );
    // An old server's generic ACK is deliberately invisible to this state
    // machine. Only the semantic split/capability replies can advance it.
    client.OnTimeout();
    assert ( client.State() == NegotiationState::Legacy && !client.CanSendPolyIn() );

    assert ( client.Begin() );
    assert ( client.OnSplitCapabilityReceived() );
    assert ( client.State() == NegotiationState::CapabilityQueued );
    assert ( !client.OnCapabilityResponse ( kVersion ) ); // queueing is not sending
    assert ( client.OnCapabilityRequestSent() );
    assert ( client.OnCapabilityResponse ( kVersion ) );
    assert ( client.State() == NegotiationState::ConfigurationQueued );
    assert ( client.OnConfigurationRequestSent() );
    assert ( client.OnAccept ( 7 ) );
    assert ( client.CanSendPolyIn() );

    Negotiation acceptedButIdle;
    assert ( acceptedButIdle.Begin() );
    assert ( acceptedButIdle.OnSplitCapabilityReceived() );
    assert ( acceptedButIdle.OnCapabilityRequestSent() );
    assert ( acceptedButIdle.OnCapabilityResponse ( kVersion ) );
    assert ( acceptedButIdle.OnConfigurationRequestSent() );
    assert ( acceptedButIdle.OnAccept ( 8 ) );
    acceptedButIdle.OnTimeout();
    assert ( acceptedButIdle.State() == NegotiationState::Legacy && !acceptedButIdle.CanSendPolyIn() );

    assert ( !client.OnFirstAcceptedFrame ( 6 ) );
    assert ( client.OnFirstAcceptedFrame ( 7 ) );
    assert ( client.State() == NegotiationState::AwaitingActivation );
    client.OnTimeout(); // confirmation timeout must not revert a live uplink
    assert ( client.State() == NegotiationState::AwaitingActivation && client.CanSendPolyIn() );
    assert ( !client.OnActivation ( 6 ) );
    assert ( client.OnActivation ( 7 ) );
    assert ( client.State() == NegotiationState::Active );
}

void TestIngressAutoJitterAndReanchor()
{
    // Clean logical arrivals at a two-frame target stay at the practical
    // minimum.  A missing logical frame raises the target, and clean arrivals
    // subsequently allow it to fall again.
    SessionIngress ingress;
    for ( uint32_t sequence = 100; sequence < 180; ++sequence )
    {
        ingress.ObserveArrival ( sequence, sequence - 2, 2 );
    }
    assert ( ingress.AutoTargetFrames() == kMinAutoIngressFrames );
    ingress.ObservePlayoutResult ( false, 2 );
    assert ( ingress.AutoTargetFrames() == 3 );
    for ( uint32_t sequence = 200; sequence < 700; ++sequence )
    {
        ingress.ObserveArrival ( sequence, sequence - 3, 3 );
    }
    assert ( ingress.AutoTargetFrames() == kMinAutoIngressFrames );

    // Reducing 10 to 3 must discard excess queued delay, but never rewind a
    // playout cursor already closer to the newest received frame.
    assert ( ReanchorPlayoutSequence ( 81, 100, 3 ) == 98 );
    assert ( ReanchorPlayoutSequence ( 99, 100, 3 ) == 99 );
}

void TestIngressDiscontinuityRecovery()
{
    // Once either sequence cursor lies at least a whole ring outside the
    // other, equal-rate operation cannot recover by itself.  A server/timer
    // stall makes the producer ahead; a capture-callback pause leaves the
    // consumer ahead.
    assert ( DetectPlayoutDiscontinuity ( 100, 103, 4 ) == EPlayoutDiscontinuity::None );
    assert ( DetectPlayoutDiscontinuity ( 100, 104, 4 ) == EPlayoutDiscontinuity::ProducerAhead );
    assert ( DetectPlayoutDiscontinuity ( 104, 100, 4 ) == EPlayoutDiscontinuity::ConsumerAhead );

    // A producer-ahead recovery retains the newest complete target window.
    assert ( RecoverPlayoutSequence ( 104, 2 ) == 103 );
    assert ( RecoverPlayoutSequence ( 104, 4 ) == 101 );

    // The ingress instance uses its real configured ring capacity, including
    // across uint32_t wraparound.
    SourceDescriptor descriptor{ 1, 1, 1, false };
    SessionIngress   ingress;
    assert ( ingress.Configure ( 1, false, &descriptor, 1, 4 ) );
    std::vector<uint8_t>            payload ( 1, 7 );
    const std::array<RecordView, 1> record{ MakeRecord ( 1, payload ) };
    FramePacketizer                 packetizer;
    const Datagram*                 datagrams = nullptr;
    size_t                          count     = 0;
    for ( uint32_t sequence = 0xfffffffdU; sequence != 2; ++sequence )
    {
        assert ( packetizer.Packetize ( 1, sequence, false, record.data(), record.size(), datagrams, count ) );
        assert ( count == 1 && ingress.Put ( datagrams[0].bytes.data(), datagrams[0].length ) );
    }
    assert ( ingress.GetPlayoutDiscontinuity ( 0xfffffffdU ) == EPlayoutDiscontinuity::ProducerAhead );
    assert ( ingress.GetPlayoutDiscontinuity ( 5 ) == EPlayoutDiscontinuity::ConsumerAhead );
}

void TestReturnPacketCadence()
{
    // This is the four-way server/output-frame matrix.  In particular, a
    // normal 128-sample server must issue two 64-sample return packets.
    assert ( ReturnPacketsPerServerTick ( true, false ) == 1 );
    assert ( ReturnPacketsPerServerTick ( true, true ) == 2 );
    assert ( ReturnPacketsPerServerTick ( false, false ) == 1 );
    assert ( ReturnPacketsPerServerTick ( false, true ) == 1 );
}

void TestUploadRateEstimate()
{
    // One 22-byte Opus64 mono record: 14-byte app header, 3-byte record
    // header and the conventional 77-byte transport allowance, 750 times/s.
    const std::array<uint16_t, 1> monoOpus64{ 22 };
    assert ( EstimateUploadRateKbps ( monoOpus64.data(), monoOpus64.size(), 64, 48000 ) == 696 );

    // Four 512-byte raw stereo records form two 1044-byte application
    // datagrams. At 128 samples/frame that is 375 frames/s, or 6726 kbps.
    const std::array<uint16_t, 4> rawStereo{ 512, 512, 512, 512 };
    assert ( EstimateUploadRateKbps ( rawStereo.data(), rawStereo.size(), 128, 48000 ) == 6726 );

    const std::array<uint16_t, 1> invalid{ 0 };
    assert ( EstimateUploadRateKbps ( invalid.data(), invalid.size(), 128, 48000 ) == 0 );
}

void TestRoutingValidation()
{
    const std::array<RoutingRow, 3> valid{ RoutingRow{ "Vox", 1, 0, -1 }, RoutingRow{ "Guitar", 2, 1, -1 }, RoutingRow{ "Keys", 3, 2, 3 } };
    assert ( ValidateRoutingRows ( valid.data(), valid.size(), 4 ) );
    auto duplicate          = valid;
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
    TestIngressAutoJitterAndReanchor();
    TestIngressDiscontinuityRecovery();
    TestReturnPacketCadence();
    TestUploadRateEstimate();
    TestRoutingValidation();
    std::cout << "polyinwire tests: PASS\n";
    return 0;
}
