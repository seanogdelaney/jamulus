/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#include "polyinplayout.h"

#include <algorithm>

namespace PolyIn
{
bool SessionPlayout::Configure ( const uint16_t                generation,
                                 const bool                    raw,
                                 const SourceDescriptor* const descriptors,
                                 const size_t                  descriptorCount,
                                 const size_t                  ringFrames )
{
    Reset();
    return ingress.Configure ( generation, raw, descriptors, descriptorCount, ringFrames );
}

void SessionPlayout::Reset()
{
    ingress.Reset();
    started       = false;
    primed        = false;
    firstSequence = 0;
    nextSequence  = 0;
    targetFrames  = 1;
}

bool SessionPlayout::Put ( const uint8_t* const data, const size_t length, bool* const firstFragmentForSequence, uint32_t* const sequence )
{
    if ( firstFragmentForSequence != nullptr )
        *firstFragmentForSequence = false;
    if ( sequence != nullptr )
        *sequence = 0;

    bool     first            = false;
    uint32_t acceptedSequence = 0;
    if ( !ingress.Put ( data, length, &first, &acceptedSequence ) )
        return false;

    if ( first && started )
        ingress.ObserveArrival ( acceptedSequence, nextSequence, targetFrames );
    if ( firstFragmentForSequence != nullptr )
        *firstFragmentForSequence = first;
    if ( sequence != nullptr )
        *sequence = acceptedSequence;
    return true;
}

void SessionPlayout::Start ( const uint32_t initialSequence, const int initialTargetFrames )
{
    started       = true;
    primed        = false;
    firstSequence = initialSequence;
    nextSequence  = initialSequence;
    SetTargetFrames ( initialTargetFrames );
}

bool SessionPlayout::SetTargetFrames ( const int newTargetFrames )
{
    if ( newTargetFrames < 1 || ingress.RingFrames() == 0 || static_cast<size_t> ( newTargetFrames ) > ingress.RingFrames() )
        return false;

    const int oldTarget = targetFrames;
    targetFrames        = newTargetFrames;
    if ( !started || newTargetFrames == oldTarget )
        return true;

    if ( newTargetFrames < oldTarget && primed && ingress.HasHighestSequence() )
    {
        nextSequence  = ReanchorPlayoutSequence ( nextSequence, ingress.HighestSequence(), newTargetFrames );
        firstSequence = nextSequence;
    }
    else if ( newTargetFrames > oldTarget )
    {
        firstSequence = nextSequence;
        primed        = false;
    }
    return true;
}

void SessionPlayout::ReprimeAfterConsumerUnderrun()
{
    firstSequence = ingress.HighestSequence() + 1;
    nextSequence  = firstSequence;
    primed        = false;
}

EPlayoutResult SessionPlayout::GetNext ( RecordView* const records, const size_t capacity, uint32_t* const sequence )
{
    if ( records == nullptr || capacity < ingress.DescriptorCount() || !started )
        return EPlayoutResult::WaitingForBuffer;

    const uint32_t expectedSequence = primed ? nextSequence : firstSequence;
    switch ( ingress.GetPlayoutDiscontinuity ( expectedSequence ) )
    {
    case EPlayoutDiscontinuity::ProducerAhead:
        firstSequence = RecoverPlayoutSequence ( ingress.HighestSequence(), targetFrames );
        nextSequence  = firstSequence;
        primed        = false;
        break;

    case EPlayoutDiscontinuity::ConsumerAhead:
        ingress.ObservePlayoutResult ( false, targetFrames );
        ReprimeAfterConsumerUnderrun();
        break;

    case EPlayoutDiscontinuity::None:
        break;
    }

    if ( !primed )
    {
        const uint32_t needed = firstSequence + static_cast<uint32_t> ( std::max ( 1, targetFrames ) - 1 );
        if ( !ingress.HasHighestSequence() || SequenceBefore ( ingress.HighestSequence(), needed ) )
            return EPlayoutResult::WaitingForBuffer;
        nextSequence = firstSequence;
        primed       = true;
    }

    const uint32_t requested = nextSequence;
    if ( sequence != nullptr )
        *sequence = requested;
    const bool haveFrame = ingress.Read ( requested, records, capacity );
    ingress.ObservePlayoutResult ( haveFrame, targetFrames );

    if ( !haveFrame && ingress.HasHighestSequence() && IsConsumerUnderrun ( requested, ingress.HighestSequence() ) )
    {
        // A producer pause is not packet loss.  Advancing here would leave the
        // consumer permanently one frame ahead at equal producer/consumer rate.
        ReprimeAfterConsumerUnderrun();
    }
    else
    {
        // A newer frame is already present, so a missing requested sequence is
        // an ordinary loss hole.  Advance and let source-local PLC conceal it.
        ++nextSequence;
    }
    return haveFrame ? EPlayoutResult::FrameAvailable : EPlayoutResult::MissingFrame;
}

int FrameCadence::FramesToRead ( const bool serverUsesDoubleSystemFrameSize, const bool sourceUses64SampleFrames )
{
    if ( !serverUsesDoubleSystemFrameSize && !sourceUses64SampleFrames )
    {
        const int result     = opusHalfFramePending ? 0 : 1;
        opusHalfFramePending = !opusHalfFramePending;
        return result;
    }
    return serverUsesDoubleSystemFrameSize && sourceUses64SampleFrames ? 2 : 1;
}

} // namespace PolyIn
