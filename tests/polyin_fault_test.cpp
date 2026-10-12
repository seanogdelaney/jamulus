/* Deterministic transport fault schedules: no network socket or Qt required.
 * Exercises the real poly-in packetizer, fragment assembler and shared cursor.
 */
#include "../src/polyinplayout.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace PolyIn;

namespace
{
struct Rig
{
    explicit Rig ( int target = 2, size_t sources = 1, size_t ring = 20, uint16_t generation = 51, uint16_t bytes = 1 ) :
        generation ( generation ),
        bytes ( bytes )
    {
        for ( size_t i = 0; i < sources; ++i )
            desc[i] = SourceDescriptor{ static_cast<uint8_t> ( i + 1 ), 1, bytes, false };
        count = sources;
        assert ( playout.Configure ( generation, false, desc.data(), count, ring ) );
        assert ( playout.SetTargetFrames ( target ) );
    }
    void Start ( uint32_t sequence ) { playout.Start ( sequence, playout.TargetFrames() ); }
    void Inject ( uint32_t sequence, uint16_t withGeneration = 0, size_t fragmentLimit = kMaxFragments )
    {
        std::array<std::array<uint8_t, kMaxRawStereoPayloadBytes>, kMaxSourceRows> samples{};
        std::array<RecordView, kMaxSourceRows>                                     records{};
        for ( size_t i = 0; i < count; ++i )
        {
            samples[i].fill ( static_cast<uint8_t> ( sequence + i ) );
            records[i] = RecordView{ desc[i].key, samples[i].data(), bytes };
        }
        const Datagram* datagrams    = nullptr;
        size_t          numDatagrams = 0;
        assert (
            packetizer.Packetize ( withGeneration ? withGeneration : generation, sequence, false, records.data(), count, datagrams, numDatagrams ) );
        assert ( numDatagrams > 0 );
        for ( size_t i = 0; i < numDatagrams && i < fragmentLimit; ++i )
        {
            const bool expected = withGeneration == 0 || withGeneration == generation;
            const bool accepted = playout.Put ( datagrams[i].bytes.data(), datagrams[i].length );
            assert ( accepted == expected );
        }
    }
    EPlayoutResult Read ( uint32_t& seq )
    {
        records = {};
        return playout.GetNext ( records.data(), records.size(), &seq );
    }
    uint16_t                                     generation;
    uint16_t                                     bytes;
    size_t                                       count = 0;
    std::array<SourceDescriptor, kMaxSourceRows> desc{};
    std::array<RecordView, kMaxSourceRows>       records{};
    SessionPlayout                               playout;
    FramePacketizer                              packetizer;
};

void BurstNineFramesBehindCannotPersist()
{
    Rig rig ( 2, 1, 20 );
    rig.Start ( 100 );
    rig.Inject ( 100 );
    rig.Inject ( 101 );
    uint32_t played = 0;
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 100 );
    for ( uint32_t arrival = 102; arrival <= 110; ++arrival )
        rig.Inject ( arrival );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 109 );
    for ( uint32_t arrival = 111; arrival < 800; ++arrival )
    {
        rig.Inject ( arrival );
        assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable );
        assert ( played == arrival - 1 );
    }
}
void ReorderDuplicateLossAndWrap()
{
    Rig rig ( 2, 1, 20 );
    rig.Start ( 0xfffffffcU );
    rig.Inject ( 0xfffffffdU );
    rig.Inject ( 0xfffffffcU );
    uint32_t played = 0;
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 0xfffffffcU );
    rig.Inject ( 0xffffffffU ); // 0xfffffffe missing, loss should be concealed
    assert ( rig.Read ( played ) == EPlayoutResult::MissingFrame && played == 0xfffffffdU + 1 );
    rig.Inject ( 0 );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 0xffffffffU );
    rig.Inject ( 1 );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 0 );
    rig.Inject ( 2 );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 1 );
}
void PartialFragmentAndStaleGeneration()
{
    // Four substantial per-source records require multiple wire fragments.
    Rig rig ( 2, 4, 20, 51, 512 );
    rig.Start ( 90 );
    rig.Inject ( 90, 0, 1 ); // drop later fragment, preserving first source(s)
    rig.Inject ( 91 );
    uint32_t played = 0;
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 90 );
    int present = 0;
    for ( size_t i = 0; i < rig.count; ++i )
        present += rig.records[i].data != nullptr;
    assert ( present >= 1 && present < 4 );
    rig.Inject ( 92, 50 ); // datagram from a previous logical session
    rig.Inject ( 92 );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 91 );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 92 );
}
void BlackoutAndResume()
{
    Rig rig ( 2 );
    rig.Start ( 100 );
    rig.Inject ( 100 );
    rig.Inject ( 101 );
    uint32_t played = 0;
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable );
    assert ( rig.Read ( played ) == EPlayoutResult::MissingFrame );
    for ( int i = 0; i < 20; ++i )
        assert ( rig.Read ( played ) == EPlayoutResult::WaitingForBuffer );
    rig.Inject ( 102 );
    rig.Inject ( 103 );
    assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == 102 );
    for ( uint32_t seq = 104; seq < 124; ++seq )
    {
        rig.Inject ( seq );
        assert ( rig.Read ( played ) == EPlayoutResult::FrameAvailable && played == seq - 1 );
    }
}
} // namespace
int main()
{
    BurstNineFramesBehindCannotPersist();
    ReorderDuplicateLossAndWrap();
    PartialFragmentAndStaleGeneration();
    BlackoutAndResume();
    std::cout << "poly-in network fault tests: PASS\n";
}
