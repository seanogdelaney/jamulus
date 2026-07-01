# Jamulus multi-source session patch

## Base and scope

Base: supplied clean `jamulus-main` tree; `Jamulus.pro` reports `VERSION = 3.12.2dev`.
This patch implements one UDP/session endpoint with one legacy-format return mix and
one-or-more independently mixed, visible source faders. It deliberately does not
reuse the supplied older client-only multi-client architecture.

`ARCHITECTURE_REVIEW.md` records the required pre-edit design gate, ownership split,
wire bounds, session promotion and the one-source assumptions found in HEAD.

## Architecture

* `CChannel` remains a physical session: address, reliable protocol, timeout, one
  return-mix encoder, one return receive path on the client, and target gain/pan arrays.
* `CServerSource` is a visible server fader with parent session ID, source descriptor,
  source-local decoder/PCM/meter/fade and recorder identity.
* The server timer decodes, meters and records by source; then mixes and sends exactly
  once by physical target session. No source creates another socket, protocol object,
  return encoder or client jitter buffer.
* The existing negotiated `CNetworkTransportProps` remains the downlink return profile.
  The Advanced descriptors describe uplink only.

## Negotiation and compatibility

| Client | Server | Result |
|---|---|---|
| old | old | unchanged legacy |
| old | patched | unchanged one-source legacy session |
| patched Advanced | old | semantic capability timeout; unchanged saved legacy input profile |
| patched Advanced | patched | capability → split config → hidden reservation → accepted generation → first advanced frame promotes source map |

Only `MULTISOURCE_CAPS` is positive capability evidence. An old server's generic ACK
to the unknown request is ignored. Source-map/table edits must be made while
disconnected and apply on the next connection. In the client UI the Advanced table,
input mode, quality/Raw choice and Small Network Buffers setting are locked from
startup until disconnect:
**“Advanced routing is fixed for this connection. Disconnect to change it.”**
An accepted packet with an unknown/stale generation is dropped.

### Deterministic startup follow-on

The initial implementation started its capability/configuration timeout when a
message was appended to Jamulus's ACK-gated reliable FIFO. On a busy first
connection that message could still be behind ordinary startup traffic, making a
capable server look unsupported. This follow-on makes every stage explicit:

```text
legacy → split reply → caps request physically sent → semantic caps reply
       → source config physically sent → accept → first Advanced frame → active confirmation
```

`MULTISOURCE_CAPS` now means the server's split-message prerequisite is complete
and it is ready to receive configuration. The new `MULTISOURCE_ACTIVE` control
message confirms the server has atomically replaced the temporary legacy fader
after the first valid generation-tagged frame. Generic ACKs remain irrelevant to
capability. The status line identifies a split timeout, caps timeout, map rejection,
accept-without-first-frame, or missing activation confirmation.

## Wire format and bounds

See `docs/JAMULUS_PROTOCOL.md` for the precise version-1 packet. It uses 1200-byte
application UDP datagrams, explicit codec/Raw descriptors, one codec-frame sequence
per session, fixed maximum fragment count of 32, and a preallocated ingress ring.
A missing fragment only makes its source records unavailable; it does not discard
records from other fragments in that codec frame.

## UI and capture backends

Advanced adds a persisted source table with **Fader icon**, **Fader tag**, **Ch1** and
**Ch2**, plus add/remove/up/down controls. Tags and physical channel assignments are
validated. Legacy mono/stereo settings remain separately persisted as the fallback and
return profile; legacy input pan/mix/reverb controls are disabled in Advanced mode.

The preallocated full-capture view is implemented by ASIO, CoreAudio macOS, and JACK.
Other backends retain their legacy stereo callback and do not offer Advanced mode.
JACK registers distinct input ports for all available capture channels.

## Advanced ingress auto-jitter follow-on

The initial multi-source session implementation inherited the legacy server
bootstrap target (`DEF_NET_BUF_SIZE_NUM_BL`, normally 10 logical frames) and
then stopped calling the legacy `CNetBufWithStats` auto-tuner after promotion.
That made a low-jitter LAN report and retain roughly ten server frames.

This follow-on patch adds a fixed-state, session-level estimator to
`MultiSource::SessionIngress`. It observes the first valid fragment of each
logical capture-frame sequence and treats a sequence as arrived when any
fragment arrives; a lost fragment therefore remains source-local PLC/silence,
not a session jitter event. Auto mode begins at two logical frames, increases
on a late/missing logical frame, and falls back after clean observations. A
reduction reanchors the playout sequence to
`highestReceived - (targetFrames - 1)`, dropping excess unplayed backlog rather
than merely changing the value shown in the client.

The existing jitter-buffer protocol remains the reporting mechanism: when the
Advanced server target changes in auto mode, the client receives the normal
server jitter-size message and its delay estimate follows the actual target.
Manual server-jitter settings remain supported and retain the requested target.

## Intentional first-version constraints

* All sources in one session share `CT_OPUS` or `CT_OPUS64`, frame cadence and Raw policy.
  Mono/stereo shape, payload length, tag and icon are source-local.
* The server's configured channel cap applies to physical user sessions, preserving the
  legacy one-user/one-channel admission meaning. Visible source faders use the fixed
  compile-time pool; reservations are atomic and hidden.
* Advanced ingress uses the existing one server jitter setting as its session target;
  no misleading per-source server jitter control is added.
* Source-local monitoring state is preallocated. Its controls are not exposed as a
  separate first-version UI surface; defaults preserve ordinary centered mono/direct
  stereo local-monitor behavior.
* There is deliberately no live Advanced map reconfiguration. Configure source rows
  while disconnected; disconnect to apply a changed table, input mapping, source
  format, tag/icon, quality/Raw policy or network-frame size.

## Added focused tests

`tests/multisourcewire_tests.pro` builds a deterministic, non-Qt test harness for:

* mixed mono/stereo serializer/deserializer and Raw flag;
* bounded multi-fragment reassembly where one fragment loss preserves other records;
* malformed/truncated/duplicate/unknown-source rejection;
* capability/timeout/acceptance negotiation states;
* source-routing duplicate-channel validation.

Run with qmake when available:

```sh
cd tests
qmake multisourcewire_tests.pro
make
./multisourcewire_tests
```

The same pure harness was run in this environment without Qt:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/multisourcewire_test.cpp \
  src/multisourcewire.cpp -o tests/multisourcewire_test
./tests/multisourcewire_test
```

Result: `multisourcewire tests: PASS`.

A full Jamulus qmake build was not run here because the environment has no `qmake` and
no Qt development headers/libraries. The supplied patch is also checked by applying it
to a fresh copy of the supplied base tree and comparing the resulting source tree.
