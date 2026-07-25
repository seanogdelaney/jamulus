/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#include "polysession.h"

namespace PolyIn
{
constexpr int SessionSourceMap::kInvalidSourceID;

void SessionSourceMap::ClearNegotiatedSources()
{
    sourceIDs.fill ( kInvalidSourceID );
    sourceCount            = 0;
    generation             = 0;
    preparedAge            = 0;
    preparedTimeoutArmed   = false;
    promotionQueued        = false;
    promotionFirstSequence = 0;
}

void SessionSourceMap::Reset()
{
    state          = State::Legacy;
    legacySourceID = kInvalidSourceID;
    ClearNegotiatedSources();
}

bool SessionSourceMap::SetLegacySource ( const int sourceID )
{
    if ( state != State::Legacy || sourceID < 0 )
        return false;
    legacySourceID = sourceID;
    return true;
}

bool SessionSourceMap::Prepare ( const uint16_t newGeneration, const int* const newSourceIDs, const size_t newSourceCount )
{
    if ( state != State::Legacy || legacySourceID < 0 || newGeneration == 0 || newSourceIDs == nullptr || newSourceCount == 0 ||
         newSourceCount > sourceIDs.size() )
    {
        return false;
    }
    for ( size_t i = 0; i < newSourceCount; ++i )
    {
        if ( newSourceIDs[i] < 0 )
            return false;
        for ( size_t earlier = 0; earlier < i; ++earlier )
        {
            if ( newSourceIDs[earlier] == newSourceIDs[i] )
                return false;
        }
    }

    ClearNegotiatedSources();
    for ( size_t i = 0; i < newSourceCount; ++i )
        sourceIDs[i] = newSourceIDs[i];
    sourceCount = newSourceCount;
    generation  = newGeneration;
    state       = State::Prepared;
    return true;
}

bool SessionSourceMap::QueuePromotion ( const uint32_t firstSequence )
{
    if ( state != State::Prepared || promotionQueued )
        return false;
    promotionQueued        = true;
    promotionFirstSequence = firstSequence;
    return true;
}

bool SessionSourceMap::TakeQueuedPromotion ( uint32_t& firstSequence )
{
    if ( state != State::Prepared || !promotionQueued )
        return false;
    firstSequence   = promotionFirstSequence;
    promotionQueued = false;
    return true;
}

bool SessionSourceMap::Activate ( const uint16_t acceptedGeneration )
{
    if ( state != State::Prepared || acceptedGeneration == 0 || acceptedGeneration != generation )
        return false;
    state                  = State::Active;
    legacySourceID         = kInvalidSourceID;
    preparedAge            = 0;
    preparedTimeoutArmed   = false;
    promotionQueued        = false;
    return true;
}

bool SessionSourceMap::ArmPreparedTimeout()
{
    if ( state != State::Prepared || promotionQueued || preparedTimeoutArmed )
        return false;
    preparedAge          = 0;
    preparedTimeoutArmed = true;
    return true;
}

bool SessionSourceMap::AdvancePreparedAge ( const uint32_t elapsedSamples, const uint32_t timeoutSamples )
{
    if ( state != State::Prepared || promotionQueued || !preparedTimeoutArmed || timeoutSamples == 0 )
        return false;
    preparedAge += elapsedSamples;
    return preparedAge >= timeoutSamples;
}

void SessionSourceMap::CancelPreparation()
{
    if ( state != State::Prepared )
        return;
    state = State::Legacy;
    ClearNegotiatedSources();
}

int SessionSourceMap::SourceAt ( const size_t index ) const
{
    return index < sourceCount ? sourceIDs[index] : kInvalidSourceID;
}

bool SessionSourceMap::OwnsSource ( const int sourceID ) const
{
    if ( sourceID == legacySourceID && legacySourceID >= 0 )
        return true;
    for ( size_t i = 0; i < sourceCount; ++i )
    {
        if ( sourceIDs[i] == sourceID )
            return true;
    }
    return false;
}

int SessionSourceMap::IdentitySourceID() const
{
    if ( state == State::Active )
        return SourceAt ( 0 );
    return legacySourceID;
}

} // namespace PolyIn
