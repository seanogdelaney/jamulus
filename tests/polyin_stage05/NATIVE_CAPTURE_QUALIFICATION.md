# Native macOS CoreAudio / Windows ASIO poly-in capture extension (F09)

Applies **after** the exact Stage 5 C7 self-reviewed cumulative patch set. No Stage 5 files/patches are replaced. This is an opt-in native sound-backend extension; it does not change the wire format, client encoder, control plane, server, stock receiver, sound device enumeration, or independent-source IDs.

## Reuse and channel semantics

- **CoreAudio (macOS):** reuse the original input `AudioDeviceIOProc` callback and its stock `vecNumInBufChan` mapping of the HAL `AudioBufferList`. The existing Float32 to 16-bit conversion is applied to `N` physically ordered input lanes and stored before `ProcessCallback`. No additional IOProc, stream, clock, background worker, or allocations on the callback.
- **ASIO (Windows):** reuse the already allocated/queried physical input channel buffers and *the exact stock sample-format conversion switch*. Two passes remain the original selected/mixed stock left/right; a further `N` passes through the same switch populate the source-local capture frame. No separate conversion table or ASIO device stream. Note that Windows native Jamulus uses ASIO, not WASAPI; this patch intentionally does not implement a new WASAPI backend.
- Source 1 is physical input 1, source 2 is physical input 2, etc. on **N > 2**. The independent poly-in sources are mono, matching the accepted Stage 5 producer descriptors. The normal Jamulus stereo return and stock local channel selections remain unchanged. For **N = 2**, the existing selected stereo inputs are used; the optional extra capture buffer is absent.
- Only a selected sound device with at least `N` **physical** input lanes can enable `N` sources (up to the existing `PolyIn::MaxSources = 20`). Pseudo/mixed channels exposed by the stock four-channel selection UI do **not** count as new physical inputs.
- All buffers are preallocated while stopped; the audio callbacks use contiguous interleaved 16-bit frames in Stage 5's already-defined `PolyInCaptureFrame()` format. A changed CoreAudio buffer-list format is silenced for that tick; reduced physical channel count on `Init` retires the old extra tap so a stale callback cannot index missing channels. Stage 5 decides subsequent withdrawal/re-offer.
- Builds without multichannel opt-in retain the original stereo code path. iOS/Android remain out of scope; JACK remains unchanged.

## Reproducible local checks

```sh
# From Stage-5-plus-F09 source root
python3 tests/polyin_stage05/run_native_capture.py
git diff --check
```

The Python script extracts and compiles the *actual changed callback loops* (not a hand-copied conversion routine) against small CoreAudio/ASIO type facades, using g++ with undefined-behaviour sanitization. It checks ASIO 16-bit, 32-bit and Float32 input format handling, per-source ordering, selected legacy stereo and disabled-mode parity. It checks CoreAudio mono-separated and interleaved buffers and fail-closed buffer-size mismatch. These are component tests, **not platform compilation or device verification**.

## Required native qualification (NOT_RUN in the patch-producing Linux environment)

1. On macOS, build with the project's stock CoreAudio target and real Qt/Opus dependencies. Use a 4/8/16-channel hardware interface or BlackHole 16ch as the physical input device (configured at Jamulus's required sample rate). Start the patched client with `--polyin --polyin-sources 4` (later try 16), and the patched Stage 5 server. Route distinct test tones/PCM patterns into channels 1..N; confirm the unchanged stock receiving client sees N separate ordinary named channels and each independently controllable.
2. On Windows, build with the existing ASIO SDK, Qt and Opus. Use a genuine >=4-input ASIO driver/interface (or an ASIO-capable multichannel virtual driver). Test at least four distinct physical input signals, including 16-bit and Float32 sample types where hardware permits, and compare stock stereo with and without `--polyin`. Verify mono source isolation and one physical return mix, not N client sessions.
3. On both, cover 2/3/4/N source counts, unsupported N > hardware, device restart, hardware removal/fewer channels, negotiated denial and withdrawal, stopped/started callbacks, output playback and stock unchanged clients; include original OF jitter/loss/reconnect and L/P matrix. Test real-time CPU/allocation/underrun profiles, including maximum feasible sources. A native build alone is insufficient for the end-to-end qualification.
4. This patch makes no claim to resolve previously unqualified Stage 5 system tests. In particular, the supplied independent review ZIP is for the *prior* transition-audited Stage 5 candidate (REJECT C7), while the C7-selfreviewed Stage 5 ZIP includes a remediation that itself still requires an independent accept/reject review.
