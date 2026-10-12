# Poly-in jitter recovery and reconnect changes

## Receive-window architecture

The 20-frame default ingress ring is **storage capacity**, not the target
jitter delay. Before this refactor, the consumer could remain 9 frames behind
incoming audio with a 2-frame target indefinitely: both producer and consumer
moved one frame per tick, so the ring-capacity discontinuity condition never
fired. This is tested in `tests/polyin_fault_test.cpp` and
`TestTargetBoundedBurstLag()`.

The extracted stock `NetReceive::SequenceDelta`/`PlaceArrival` primitives
retain stock's modular sequence and placement decisions for 8-bit numbers.
`NetReceive::ForwardTargetCursor` applies the same window-placement principle
to 32-bit poly-in sequence numbers, but never rewinds already consumed source
frames. On each playout callback it drops obsolete backlog to keep playback at
or newer than `highestReceived - (targetFrames - 1)`.

A missing logical frame *within* the active window yields PLC for every source.
If only one fragment is absent, present source records still decode and only
absent records produce source-local PLC. Incomplete session frames count as
missed playout deadlines for automatic jitter adaptation, even when another
source supplied an accepted record.

The automatic ingress target starts at six frames. Its target selection and
IIR smoothing share the extracted `netjitterpolicy.h` routines with stock
`CNetBufWithStats`. The input error measurement is necessarily **different**:
stock simulates candidate buffers on its single compressed audio stream;
poly-in estimates deadline failures from one shared logical-session timeline.
It is **not** an identical implementation of stock's simulated buffers.
This difference and the filter time constants warrant live network validation.

Raising the auto target does not discard primed audio. It allows the receive
window to accumulate an increased delay naturally. Lowering the target
moves the cursor forward without replay.

## Reconnect

The poly-in control handshake includes a 32-bit request nonce, echoed in the
accept. At the same physical endpoint, a different, validated nonce denotes a
fresh logical connection epoch. The server retires any surviving previous
poly-in source map, codec state and playout state before preparing the new map.
Repeating the same request remains idempotent. Old UDP generations are rejected
by `SessionIngress` even when arriving after a replacement. A lost legacy
connectionless disconnect is therefore not a prerequisite to rejoin.

The versioned poly-in extension is **wire format version 2**, deliberately
incompatible with earlier experimental version-1 poly-in peers. Standard
Jamulus clients continue using legacy transport; automatic poly-in fallback
must be exercised against a separate unmodified stock server on macOS.

## Tests and limitations

Run `tests/run_receive_tests.sh` on the patched source tree. It compiles and
runs self-contained transport/window/packetizer/lifecycle/fault tests without
Qt. Run `qmake tests/tests.pro && make` with the configured Qt toolchain for
additional Qt-oriented projects, and run the stock-buffer test independently
with `tests/stock_buffer_tests.pro`. `tests/server_legacy_test.cpp` is an
additional QtTest source requiring linkage with the full Jamulus server
objects; it is not included in the default headless test runner.

The deterministic tests cannot establish real-world clock drift, long-term
jitter convergence, callback real-time safety, UI persistence or live reconnect
reliability. Those are gates for the macOS/Windows integration exercise.
