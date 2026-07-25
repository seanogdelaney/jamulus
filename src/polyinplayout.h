/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#pragma once

#include "polyinwire.h"

namespace PolyIn
{
enum class EPlayoutResult : uint8_t
{
    WaitingForBuffer,
    FrameAvailable,
    MissingFrame
};

// Own the complete session-level playout contract: reassembly storage,
// sequence cursor, priming, loss/underrun distinction and jitter target.
// CServer consumes only GetNext(), so its cursor cannot drift independently
// from the ring which receives packets.
class SessionPlayout
{
public:
    bool Configure ( uint16_t                generation,
                     bool                    raw,
                     const SourceDescriptor* descriptors,
                     size_t                  descriptorCount,
                     size_t                  ringFrames = kDefaultIngressFrames );
    void Reset();

    bool Put ( const uint8_t* data, size_t length, bool* firstFragmentForSequence = nullptr, uint32_t* sequence = nullptr );

    void Start ( uint32_t firstSequence, int targetFrames );
    bool SetTargetFrames ( int targetFrames );

    EPlayoutResult GetNext ( RecordView* records, size_t capacity, uint32_t* sequence = nullptr );

    size_t   DescriptorCount() const { return ingress.DescriptorCount(); }
    uint16_t Generation() const { return ingress.Generation(); }
    bool     Raw() const { return ingress.Raw(); }
    bool     Started() const { return started; }
    bool     Primed() const { return primed; }
    int      TargetFrames() const { return targetFrames; }
    uint32_t NextSequence() const { return nextSequence; }
    int      AutoTargetFrames() const { return ingress.AutoTargetFrames(); }

private:
    void ReprimeAfterConsumerUnderrun();

    SessionIngress ingress;
    bool           started       = false;
    bool           primed        = false;
    uint32_t       firstSequence = 0;
    uint32_t       nextSequence  = 0;
    int            targetFrames  = 1;
};

// Poly-in cadence only.  This deliberately does not refactor the established
// legacy decode path; it centralises the new session-level decision about how
// many multiplexed logical frames one server callback consumes.
class FrameCadence
{
public:
    void Reset() { opusHalfFramePending = false; }
    int  FramesToRead ( bool serverUsesDoubleSystemFrameSize, bool sourceUses64SampleFrames );

private:
    bool opusHalfFramePending = false;
};

} // namespace PolyIn
