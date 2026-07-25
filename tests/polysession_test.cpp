/* Deterministic non-Qt tests for Poly-in source-map lifecycle and identity. */
#include "../src/polysession.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace PolyIn;

namespace
{
void TestLifecycleAndIdentity()
{
    SessionSourceMap map;
    map.Reset();
    assert ( map.IsLegacy() );
    assert ( map.SetLegacySource ( 7 ) );
    assert ( map.IdentitySourceID() == 7 );
    assert ( map.OwnsSource ( 7 ) );

    const std::array<int, 3> sources{ 10, 11, 12 };
    assert ( map.Prepare ( 4, sources.data(), sources.size() ) );
    assert ( map.IsPrepared() && map.Generation() == 4 );
    assert ( map.LegacySourceID() == 7 );
    assert ( map.IdentitySourceID() == 7 );
    for ( const int source : sources )
        assert ( map.OwnsSource ( source ) );

    assert ( map.ArmPreparedTimeout() );
    assert ( !map.ArmPreparedTimeout() );
    assert ( map.QueuePromotion ( 100 ) );
    assert ( !map.QueuePromotion ( 101 ) );
    assert ( !map.AdvancePreparedAge ( 10000, 1 ) ); // queued activation wins over cleanup
    uint32_t firstSequence = 0;
    assert ( map.TakeQueuedPromotion ( firstSequence ) && firstSequence == 100 );
    assert ( !map.Activate ( 3 ) );
    assert ( map.Activate ( 4 ) );
    assert ( map.IsActive() );
    assert ( map.LegacySourceID() == SessionSourceMap::kInvalidSourceID );
    assert ( map.IdentitySourceID() == 10 );
    assert ( !map.OwnsSource ( 7 ) );
    for ( const int source : sources )
        assert ( map.OwnsSource ( source ) );
}

void TestPreparedTimeoutAndRetry()
{
    SessionSourceMap map;
    assert ( map.SetLegacySource ( 2 ) );
    const std::array<int, 2> first{ 20, 21 };
    assert ( map.Prepare ( 8, first.data(), first.size() ) );
    // The server arms cleanup only after the reliable ACCEPT is acknowledged.
    assert ( !map.AdvancePreparedAge ( 2000, 1000 ) );
    assert ( map.ArmPreparedTimeout() );
    assert ( !map.AdvancePreparedAge ( 399, 1000 ) );
    assert ( !map.AdvancePreparedAge ( 600, 1000 ) );
    assert ( map.AdvancePreparedAge ( 1, 1000 ) );

    // The server releases these returned IDs before cancelling the hidden map.
    assert ( map.SourceCount() == 2 && map.SourceAt ( 0 ) == 20 && map.SourceAt ( 1 ) == 21 );
    map.CancelPreparation();
    assert ( map.IsLegacy() );
    assert ( map.IdentitySourceID() == 2 );
    assert ( map.SourceCount() == 0 );

    const std::array<int, 1> retry{ 30 };
    assert ( map.Prepare ( 9, retry.data(), retry.size() ) );
    assert ( map.IsPrepared() && map.Generation() == 9 );
}

void TestResetFromEveryLifecycleState()
{
    for ( const SessionSourceMap::State state :
          { SessionSourceMap::State::Legacy, SessionSourceMap::State::Prepared, SessionSourceMap::State::Active } )
    {
        SessionSourceMap map;
        assert ( map.SetLegacySource ( 1 ) );
        const std::array<int, 2> sources{ 4, 5 };
        if ( state != SessionSourceMap::State::Legacy )
            assert ( map.Prepare ( 2, sources.data(), sources.size() ) );
        if ( state == SessionSourceMap::State::Active )
            assert ( map.Activate ( 2 ) );

        map.Reset();
        assert ( map.IsLegacy() );
        assert ( map.LegacySourceID() == SessionSourceMap::kInvalidSourceID );
        assert ( map.SourceCount() == 0 );
        assert ( !map.OwnsSource ( 1 ) && !map.OwnsSource ( 4 ) );
    }
}

void TestInvalidMapsAndResetCleanup()
{
    SessionSourceMap map;
    const std::array<int, 2> duplicate{ 4, 4 };
    assert ( !map.Prepare ( 1, duplicate.data(), duplicate.size() ) );
    assert ( map.SetLegacySource ( 1 ) );
    assert ( !map.Prepare ( 0, duplicate.data(), 1 ) );
    assert ( !map.Prepare ( 1, duplicate.data(), duplicate.size() ) );

    const std::array<int, 2> valid{ 4, 5 };
    assert ( map.Prepare ( 1, valid.data(), valid.size() ) );
    map.Reset();
    assert ( map.IsLegacy() );
    assert ( map.IdentitySourceID() == SessionSourceMap::kInvalidSourceID );
    assert ( !map.OwnsSource ( 1 ) && !map.OwnsSource ( 4 ) );
}
} // namespace

int main()
{
    TestLifecycleAndIdentity();
    TestPreparedTimeoutAndRetry();
    TestResetFromEveryLifecycleState();
    TestInvalidMapsAndResetCleanup();
    std::cout << "polysession tests: PASS\n";
    return 0;
}
