# Multi-source session architecture review

Base inspected: supplied `jamulus-main` tree, `Jamulus.pro` reports `VERSION = 3.12.2dev`.

This review was completed before production-source edits. `CChannel` remains the physical
session/endpoint primitive. A source is not a socket, `CProtocol`, or `CChannel`.

## A–H decision gate

| Gate | Status | HEAD evidence inspected | Resulting decision |
|---|---|---|---|
| A. identity / ownership | **confirmed** | `src/server.h:CServer` owns `vecChannels`, `src/server.cpp:CServer::FindChannel`, `InitChannel`, `FreeChannel`, `CreateChannelList`, `OnTimer` | Split the one index into a session-slot pool (`CChannel`) and a visible-source pool (`CServerSource`). The configured max remains a visible-source cap. Session slots are independently bounded at the same compile-time ceiling. |
| B. direction-specific transport | **confirmed** | `src/channel.h:CChannel::SetAudioStreamProperties`, `src/util.h:CNetworkTransportProps`, `src/server.cpp:DecodeReceiveData` / `MixEncodeTransmitData` | Leave the negotiated legacy `CNetworkTransportProps` on the `CChannel` as the sole return-mix profile. Add direction-specific negotiated uplink descriptors for source records; no N-channel `CNetworkTransportProps`. |
| C. client-driven promotion | **confirmed** | `src/socket.cpp:CSocket::OnDataReceived`, `src/protocol.cpp:CProtocol::ParseMessageBody`, `CreateAndSendMessage` | New `REQ_MULTISOURCE_CAPS`, `MULTISOURCE_CAPS`, `MULTISOURCE_CONFIG`, `MULTISOURCE_ACCEPT`, and `MULTISOURCE_REJECT` are reliable messages. Generic ACK is ignored as capability evidence. A reservation stays hidden until its first valid accepted-generation frame. |
| D. frame / jitter model | **confirmed** | `src/channel.h:CNetBufWithStats SockBuf`, `src/channel.cpp:CChannel::PutAudioData`, `src/server.cpp:OnTimer` | Add a bounded logical-frame reassembly ring instead of retrofitting `CNetBufWithStats`. It observes one frame sequence per physical session, admits partial frames, and records source-level absence independently. |
| E. source inputs / session outputs | **confirmed** | `src/server.cpp:CServer::OnTimer`, `DecodeReceiveDataBlocks`, `MixEncodeTransmitDataBlocks` | Decode, meter and record by visible source; mix, encode and transmit once by physical session. Session gain/pan arrays retain visible source IDs as their indexes. |
| F. own fader / local monitoring | **confirmed** | `src/client.cpp:CClient::OnClientIDReceived`, `SetRemoteChanGain`, `ProcessAudioDataIntern`; `src/audiomixerboard.cpp` uses `iMyChannelID` in own-first, auto-level and mute paths | Replace singular ownership with a fixed owned-source mask. Mixer calls mark every mapped owned fader. Headless personal-mix muting sends zero gain for each owned source. Advanced local-monitor handling has per-source gain/pan storage. |
| G. capture boundary | **adjusted** | `src/sound/soundbase.h:CSoundBase::ProcessCallback`; ASIO `vecsMultChanAudioSndCrd`, macOS CoreAudio selected stereo path, JACK two input ports; `src/client.cpp:ProcessSndCrdAudioData` | Keep legacy mutable stereo return I/O untouched and add a preallocated optional physical-capture view. ASIO, macOS CoreAudio and JACK populate it. Other backends report Advanced unavailable and remain unmodified legacy paths. |
| H. bounds / scope | **confirmed** | `src/global.h:MAX_NUM_IN_OUT_CHANNELS=64`, `MAX_NUM_CHANNELS=150`, `MAX_SIZE_BYTES_NETW_BUF=20000`; `src/protocol.cpp` split-message implementation | Client rows are bounded by `MAX_NUM_IN_OUT_CHANNELS`; visible sources by server capacity. Frames use a 1200-byte application UDP limit, fixed maximum record payload of 515 bytes (3-byte record header + 512-byte stereo Raw frame), and at most 32 fragments for 64 rows. |

## Ownership and execution model

```text
CServer
  sessionSlots[MAX_NUM_CHANNELS]
    CChannel
      address, protocol queue, timeout/ping, legacy return profile,
      outgoing return-mix encoder/conversion state, target gain/pan[visible source id]
      CMultiSourceSessionIngress (only after promotion)
  sourceSlots[MAX_NUM_CHANNELS]
    CServerSource
      visible fader id, parent session id, local key, tag/icon,
      source decoder/conversion state, decoded PCM, level meter, fade-in,
      recorder track identity

legacy session: one CChannel + one visible CServerSource
advanced session: one CChannel + N visible CServerSource objects
```

`CChannelCoreInfo` stays on the parent session. `CServerSource` stores only tag and
instrument. `CServer::CreateChannelList()` derives each ordinary fader record from those
parts (`owner — tag`, parent country/city/skill, source icon), so a later parent-info update
updates all derived source records without copied identity state.

## Promotion state machine

```text
legacy-start
  client has Advanced selected
  └─ send REQ_MULTISOURCE_CAPS after normal legacy startup
      ├─ timeout / only generic ACK / unknown server → legacy-fallback
      └─ semantic MULTISOURCE_CAPS → config-pending
          └─ send split MULTISOURCE_CONFIG
              ├─ reject / capacity error / disconnect → legacy-fallback
              └─ MULTISOURCE_ACCEPT(map,generation) → prepared
                  └─ at next codec-frame boundary send advanced fragment(s)
                      ├─ bad/stale generation → drop; prepared remains bounded
                      └─ first valid frame → atomically retire temporary legacy source,
                         expose N reserved sources, clear legacy ingress, publish one list,
                         state=active

A disconnect while prepared frees reservations. An active configuration is immutable; table
changes set “reconnect required”. Server packets are accepted only for a known source key,
prepared/active generation, bounded fragment count, and matching expected payload length.
```

A new server never probes an old client. Old clients never request the semantic capability.

## Advanced uplink frame

All integer fields are network byte order, unlike the existing reliable protocol body.

```text
0   u16  magic = 0x4d53 ("MS", non-zero)
2   u8   version = 1
3   u8   flags (bit 0: Raw; reserved bits zero)
4   u16  generation
6   u32  session-frame sequence
10  u8   fragment index
11  u8   fragment count (1..32)
12  u8   record count
13  u8   reserved = 0
14  repeated: u8 source key, u16 payload length, payload bytes
```

A source descriptor negotiated reliably supplies channel count, codec family, frame cadence,
Raw flag, exact payload length, tag/icon and assigned ordinary fader ID. The record therefore
contains no mutable metadata. The conservative application datagram maximum is 1200 bytes.
A largest supported record is `1 + 2 + 2 * 128 * sizeof(int16_t) = 515` bytes; two fit with a
14-byte header (`14 + 2*515 = 1044`). At 64 source rows, maximum required fragments is
`ceil(64 / 2) = 32`. Storage is fixed at `ringFrames * actualReservedSources * 512` bytes,
not `MAX_NUM_CHANNELS * MAX_NUM_IN_OUT_CHANNELS`.

A ring entry becomes eligible when any valid fragment for its sequence arrives. Missing
fragments only make their records absent; valid records from other fragments remain eligible.
When the expected sequence has no entry every source uses PLC/Raw silence. Frames outside the
bounded wrap-safe receive window are discarded without changing playout state.

## Timing

```text
sound callback: [codec frame 100][codec frame 101][codec frame 102]
                  └─ serialise frame 100 records/fragments
                                     └─ serialise frame 101 records/fragments
                                                                └─ serialise frame 102 records/fragments
```

The callback is not the wire timing unit. Per-source encoders are called once for each codec
frame, share the one monotonically increasing session sequence, and do no allocation or UI
work in the callback.

## One-source assumptions found

| Area | Existing assumption | Change |
|---|---|---|
| server slot/order | `vecChannels` is both address order and fader order | Separate session address order from visible source order. |
| server decoder/mixer/recording | timer decodes, records, then sends once per channel | decode/record by source; send once per session. |
| server RPC | `GetConCliParam()` reads `vecChannels` | source-facing client list resolves visible sources, with session/source totals separately available. |
| protocol routing | server lookup returns a channel ID | lookup returns session slot; fader IDs remain protocol gain/pan indexes. |
| client identity | one `PROTMESSID_CLIENT_ID` / `OnClientIDReceived` | map acceptance supplies owned-source IDs; legacy continues to use one ID. |
| mixer UI | `iMyChannelID` is a scalar | owned-source mask is used for own-first, mute, new-client and auto-level exclusion. |
| local monitor | one `fMuteOutStreamGain` | one monitor gain/pan state per owned source. |
| audio callbacks | mutable selected stereo buffer is capture and return | additional read-only physical-capture view; legacy stereo remains unchanged. |
| recording | recorder starts/stops by channel ID | one track per visible source; parent session disconnect retires all tracks together. |

## Deliberate first-version scope

* Advanced capture is offered only by ASIO, macOS CoreAudio and JACK; legacy backends remain
  traditional two-channel operation.
* All sources in one session share `CT_OPUS` or `CT_OPUS64`, frame cadence and session Raw
  policy; mono/stereo shape and payload length are source local.
* Source configuration is validated and applied only on a controlled reconnect/promotion.
* UDP records do not span fragments. A configuration whose negotiated record cannot fit the
  conservative datagram payload is rejected.
* No extra server jitter control is exposed per source. Existing session jitter control selects
  the session ingress policy when Advanced is active.
