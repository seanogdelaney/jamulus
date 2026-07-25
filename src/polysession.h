/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#pragma once

#include "polyinwire.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace PolyIn
{
class SessionSourceMap
{
public:
    enum class State : uint8_t
    {
        Legacy,
        Prepared,
        Active
    };

    static constexpr int kInvalidSourceID = -1;

    void Reset();
    bool SetLegacySource ( int sourceID );
    bool Prepare ( uint16_t generation, const int* sourceIDs, size_t sourceCount );
    bool QueuePromotion ( uint32_t firstSequence );
    bool TakeQueuedPromotion ( uint32_t& firstSequence );
    bool Activate ( uint16_t generation );
    bool ArmPreparedTimeout();
    bool AdvancePreparedAge ( uint32_t elapsedSamples, uint32_t timeoutSamples );
    void CancelPreparation();

    State    GetState() const { return state; }
    bool     IsLegacy() const { return state == State::Legacy; }
    bool     IsPrepared() const { return state == State::Prepared; }
    bool     IsActive() const { return state == State::Active; }
    bool     PromotionQueued() const { return promotionQueued; }
    uint16_t Generation() const { return generation; }
    int      LegacySourceID() const { return legacySourceID; }
    size_t   SourceCount() const { return sourceCount; }
    int      SourceAt ( size_t index ) const;
    bool     OwnsSource ( int sourceID ) const;

    // Chat/RPC events which require one representative visible source use the
    // temporary legacy fader before promotion and the first negotiated fader
    // afterwards.  Transport/session identity remains separate.
    int IdentitySourceID() const;

private:
    void ClearNegotiatedSources();

    State                           state          = State::Legacy;
    int                             legacySourceID = kInvalidSourceID;
    std::array<int, kMaxSourceRows> sourceIDs{};
    size_t                          sourceCount            = 0;
    uint16_t                        generation             = 0;
    uint64_t                        preparedAge            = 0;
    bool                            preparedTimeoutArmed   = false;
    bool                            promotionQueued        = false;
    uint32_t                        promotionFirstSequence = 0;
};

} // namespace PolyIn
