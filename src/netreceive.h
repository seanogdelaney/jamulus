/******************************************************************************\
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Common sequence-window positioning primitives. No Qt, allocation or I/O.
\******************************************************************************/
#pragma once
#include <cstdint>
#include <limits>

namespace NetReceive
{
// The signed modular offset is only unambiguous inside half the counter range.
// The 8-bit operation exactly reproduces CNetBuf's historic -128..127 rule.
inline int32_t SequenceDelta (uint8_t received, uint8_t expected)
{
    return static_cast<int8_t>(static_cast<uint8_t>(received - expected));
}
inline int32_t SequenceDelta (uint32_t received, uint32_t expected)
{
    const uint32_t delta = received - expected;
    if (delta < UINT32_C(0x80000000)) return static_cast<int32_t>(delta);
    return static_cast<int32_t>(static_cast<int64_t>(delta) - INT64_C(0x100000000));
}
struct WindowPlacement
{
    int backward;    // number of blocks the window moves towards earlier sequence
    int forward;     // number of blocks the window moves towards later sequence
    int offset;      // packet location after shifting, relative to new get position
};
// The stock algorithm intentionally moves backwards even for packets that are
// too old for the current window. Poly-in uses the forward-only variant below
// once a logical frame has already been consumed.
inline WindowPlacement PlaceArrival (int32_t delta, int capacity)
{
    if (delta < 0) return {-delta, 0, 0};
    if (delta >= capacity) return {0, delta - capacity + 1, capacity - 1};
    return {0, 0, delta};
}
// A physical storage ring is not a jitter target. When the producer runs far
// ahead of the consumer, discard the excess backlog to bound latency.
inline uint32_t ForwardTargetCursor (uint32_t next, uint32_t highest, int target)
{
    if (target < 1) target = 1;
    const uint32_t desired = highest - static_cast<uint32_t>(target - 1);
    return SequenceDelta(desired, next) > 0 ? desired : next;
}
} // namespace NetReceive
