/* Deterministic non-Qt tests for Poly-in playout and callback cadence. */
#include "../src/polyinplayout.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace PolyIn;

namespace
{
class PlayoutHarness
{
public:
    explicit PlayoutHarness ( const int targetFrames, const size_t ringFrames = 8, const size_t sources = 1 )
    {
        assert ( sources > 0 && sources <= descriptors.size() );
        sourceCount = sources;
        for ( size_t i = 0; i < sourceCount; ++i )
            descriptors[i] = SourceDescriptor{ static_cast<uint8_t> ( i + 1 ), 1, 1, false };
        assert ( playout.Configure ( generation, false, descriptors.data(), sourceCount, ringFrames ) );
        assert ( playout.SetTargetFrames ( targetFrames ) );
    }

    void Start ( const uint32_t sequence ) { playout.Start ( sequence, playout.TargetFrames() ); }

    void Put ( const uint32_t sequence )
    {
        std::array<uint8_t, kMaxSourceRows> payload{};
        std::array<RecordView, kMaxSourceRows> records{};
        for ( size_t i = 0; i < sourceCount; ++i )
        {
            payload[i] = static_cast<uint8_t> ( sequence + i );
            records[i] = RecordView{ descriptors[i].key, &payload[i], 1 };
        }
        const Datagram* datagrams = nullptr;
        size_t          count     = 0;
        assert ( packetizer.Packetize ( generation, sequence, false, records.data(), sourceCount, datagrams, count ) );
        for ( size_t i = 0; i < count; ++i )
        {
            bool     first    = false;
            uint32_t accepted = 0;
            assert ( playout.Put ( datagrams[i].bytes.data(), datagrams[i].length, &first, &accepted ) );
            assert ( accepted == sequence );
            assert ( first == ( i == 0 ) );
        }
    }

    EPlayoutResult Get ( uint32_t& sequence, std::array<RecordView, kMaxSourceRows>& records )
    {
        records = {};
        return playout.GetNext ( records.data(), records.size(), &sequence );
    }

    SessionPlayout playout;

private:
    static constexpr uint16_t generation = 9;
    size_t sourceCount = 0;
    std::array<SourceDescriptor, kMaxSourceRows> descriptors{};
    FramePacketizer packetizer;
};

void TestUnderrunRecoveryAtLowTargets()
{
    for ( int target = 1; target <= 3; ++target )
    {
        PlayoutHarness harness ( target );
        harness.Start ( 100 );
        for ( uint32_t sequence = 100; sequence < 100U + static_cast<uint32_t> ( target ); ++sequence )
            harness.Put ( sequence );

        std::array<RecordView, kMaxSourceRows> records{};
        uint32_t sequence = 0;
        assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable );
        assert ( sequence == 100 && records[0].data[0] == 100 );

        // Drain the initial target window. The following tick runs ahead of the
        // producer and must re-prime rather than advancing into a permanent chase.
        for ( int i = 1; i < target; ++i )
            assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable );
        const uint32_t missed = 100U + static_cast<uint32_t> ( target );
        assert ( harness.Get ( sequence, records ) == EPlayoutResult::MissingFrame );
        assert ( sequence == missed );
        assert ( !harness.playout.Primed() );
        assert ( harness.playout.NextSequence() == missed );

        for ( uint32_t next = missed; next < missed + static_cast<uint32_t> ( target ); ++next )
            harness.Put ( next );
        assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable );
        assert ( sequence == missed && records[0].data[0] == static_cast<uint8_t> ( missed ) );
    }
}

void TestLossAdvancesWithoutRepriming()
{
    PlayoutHarness harness ( 1 );
    harness.Start ( 100 );
    harness.Put ( 100 );
    harness.Put ( 102 );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 100 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::MissingFrame && sequence == 101 );
    assert ( harness.playout.Primed() );
    assert ( harness.playout.NextSequence() == 102 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 102 );
}

void TestReorderingAndBurstLossRemainSynchronized()
{
    PlayoutHarness harness ( 2 );
    harness.Start ( 100 );

    // Arrival order must not determine playout order.
    harness.Put ( 101 );
    harness.Put ( 100 );
    harness.Put ( 104 );
    harness.Put ( 105 );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 100 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 101 );

    // Newer frames prove that 102 and 103 are packet-loss holes, so playout
    // advances through PLC without dropping out of the primed state.
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::MissingFrame && sequence == 102 );
    assert ( harness.playout.Primed() );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::MissingFrame && sequence == 103 );
    assert ( harness.playout.Primed() );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 104 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 105 );
}

void TestWraparoundUnderrunRecovery()
{
    PlayoutHarness harness ( 2 );
    harness.Start ( 0xfffffffeU );
    harness.Put ( 0xfffffffeU );
    harness.Put ( 0xffffffffU );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 0xfffffffeU );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 0xffffffffU );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::MissingFrame && sequence == 0 );
    harness.Put ( 0 );
    harness.Put ( 1 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 0 );
}

void TestProducerOverrunReanchorsToRecentWindow()
{
    PlayoutHarness harness ( 2, 4 );
    harness.Start ( 100 );
    for ( uint32_t sequence = 100; sequence <= 106; ++sequence )
        harness.Put ( sequence );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable );
    assert ( sequence == 105 );
    assert ( records[0].data[0] == 105 );
}

void TestTargetChangesDoNotReplay()
{
    PlayoutHarness harness ( 3 );
    harness.Start ( 100 );
    for ( uint32_t sequence = 100; sequence <= 104; ++sequence )
        harness.Put ( sequence );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 100 );
    assert ( harness.playout.SetTargetFrames ( 1 ) );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 104 );

    assert ( harness.playout.SetTargetFrames ( 3 ) );
    harness.Put ( 105 );
    harness.Put ( 106 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::WaitingForBuffer );
    harness.Put ( 107 );
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable && sequence == 105 );
}

void TestMultipleSourceRecordsRemainAligned()
{
    PlayoutHarness harness ( 1, 8, 3 );
    harness.Start ( 77 );
    harness.Put ( 77 );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable );
    assert ( sequence == 77 );
    for ( size_t source = 0; source < 3; ++source )
    {
        assert ( records[source].key == source + 1 );
        assert ( records[source].data != nullptr );
        assert ( records[source].data[0] == static_cast<uint8_t> ( 77 + source ) );
    }
}

void TestAutoJitterStatistic()
{
    PlayoutHarness harness ( 2 );
    harness.Start ( 100 );
    for ( uint32_t sequence = 100; sequence < 180; ++sequence )
        harness.Put ( sequence );
    assert ( harness.playout.AutoTargetFrames() == kMinAutoIngressFrames );

    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    while ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable )
    {}
    assert ( harness.playout.AutoTargetFrames() >= 3 );
}

void CheckCadenceConsumption ( const bool serverDouble, const bool source64, const int ticks, const int expectedFrames )
{
    PlayoutHarness harness ( 1, 32 );
    harness.Start ( 10 );
    for ( int frame = 0; frame < expectedFrames; ++frame )
        harness.Put ( 10U + static_cast<uint32_t> ( frame ) );

    FrameCadence cadence;
    std::array<RecordView, kMaxSourceRows> records{};
    uint32_t sequence = 0;
    int consumed = 0;
    for ( int tick = 0; tick < ticks; ++tick )
    {
        const int frames = cadence.FramesToRead ( serverDouble, source64 );
        for ( int frame = 0; frame < frames; ++frame )
        {
            assert ( harness.Get ( sequence, records ) == EPlayoutResult::FrameAvailable );
            assert ( sequence == 10U + static_cast<uint32_t> ( consumed ) );
            ++consumed;
        }
    }
    assert ( consumed == expectedFrames );
}

void TestCadenceConsumesEachLogicalFrameExactlyOnce()
{
    CheckCadenceConsumption ( false, false, 4, 2 ); // 64-sample server, 128-sample Opus
    CheckCadenceConsumption ( false, true, 4, 4 );  // 64-sample server, Opus64
    CheckCadenceConsumption ( true, false, 4, 4 );  // 128-sample server, 128-sample Opus
    CheckCadenceConsumption ( true, true, 4, 8 );   // 128-sample server, Opus64
}

void TestDescriptorFormatMatrix()
{
    for ( const bool raw : { false, true } )
    {
        for ( const uint8_t channels : { uint8_t ( 1 ), uint8_t ( 2 ) } )
        {
            const SourceDescriptor descriptor{ 1, channels, 4, raw };
            SessionPlayout playout;
            assert ( playout.Configure ( 3, raw, &descriptor, 1, 4 ) );
            assert ( playout.SetTargetFrames ( 1 ) );
            playout.Start ( 20, 1 );

            const std::array<uint8_t, 4> payload{ 1, 2, 3, 4 };
            const RecordView record{ 1, payload.data(), static_cast<uint16_t> ( payload.size() ) };
            FramePacketizer packetizer;
            const Datagram* datagrams = nullptr;
            size_t count = 0;
            assert ( packetizer.Packetize ( 3, 20, raw, &record, 1, datagrams, count ) );
            assert ( count == 1 && playout.Put ( datagrams[0].bytes.data(), datagrams[0].length ) );

            std::array<RecordView, kMaxSourceRows> records{};
            uint32_t sequence = 0;
            assert ( playout.GetNext ( records.data(), records.size(), &sequence ) == EPlayoutResult::FrameAvailable );
            assert ( sequence == 20 && records[0].length == payload.size() );
            for ( size_t i = 0; i < payload.size(); ++i )
                assert ( records[0].data[i] == payload[i] );
        }
    }
}

void TestFrameCadenceMatrix()
{
    FrameCadence cadence;
    const std::array<int, 4> opusOn64{ cadence.FramesToRead ( false, false ),
                                      cadence.FramesToRead ( false, false ),
                                      cadence.FramesToRead ( false, false ),
                                      cadence.FramesToRead ( false, false ) };
    assert ( ( opusOn64 == std::array<int, 4>{ 1, 0, 1, 0 } ) );

    cadence.Reset();
    for ( int i = 0; i < 4; ++i )
        assert ( cadence.FramesToRead ( false, true ) == 1 );
    cadence.Reset();
    for ( int i = 0; i < 4; ++i )
        assert ( cadence.FramesToRead ( true, false ) == 1 );
    cadence.Reset();
    for ( int i = 0; i < 4; ++i )
        assert ( cadence.FramesToRead ( true, true ) == 2 );

    assert ( ReturnPacketsPerServerTick ( true, false ) == 1 );
    assert ( ReturnPacketsPerServerTick ( true, true ) == 2 );
    assert ( ReturnPacketsPerServerTick ( false, false ) == 1 );
    assert ( ReturnPacketsPerServerTick ( false, true ) == 1 );
}
} // namespace

int main()
{
    TestUnderrunRecoveryAtLowTargets();
    TestLossAdvancesWithoutRepriming();
    TestReorderingAndBurstLossRemainSynchronized();
    TestWraparoundUnderrunRecovery();
    TestProducerOverrunReanchorsToRecentWindow();
    TestTargetChangesDoNotReplay();
    TestMultipleSourceRecordsRemainAligned();
    TestAutoJitterStatistic();
    TestCadenceConsumesEachLogicalFrameExactlyOnce();
    TestDescriptorFormatMatrix();
    TestFrameCadenceMatrix();
    std::cout << "polyinplayout tests: PASS\n";
    return 0;
}
