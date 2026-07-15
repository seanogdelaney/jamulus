### Copyright (c) 2022-2026

Author(s):
* Emlyn Bolton
* The Jamulus Development Team

As of Jamulus 3.12.1dev (commit eb172d47): All new source code contributions must be licensed
under AGPL 3.0 or any later version.

Existing code: Code contributed before 3.12.1dev (commit eb172d47) was licensed under GPL 2.0+.
This code will be licensed under GPL 3.0 (or any later version) from
3.12.1dev (commit eb172d47).  When distributed as part of Jamulus, the AGPL 3.0 terms govern
the combined work, including network use provisions.

---

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see [<https://www.gnu.org/licenses/>](https://www.gnu.org/licenses/).

---

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see [<https://www.gnu.org/licenses/>](https://www.gnu.org/licenses/).

# The Jamulus Audio Protocol

Jamulus uses connectionless UDP packets to communicate between the client and server, and additionally for directory server registration. The `src/protocol.cpp` file contains much of the details of the packets themselves, whereas this document is intended to form a higher-level view of the protocol interactions.
Some of the messages need to be acknowledged, some do not. If a message ID is less than 1000, the message must be acknowledged in under `SEND_MESS_TIMEOUT_MS` ms.

All of this information can be discovered from reading the code, but hopefully is quicker to digest when available in one location. There is a wireshark dissector available too, [here](https://github.com/softins/jamulus-wireshark), if you would like to inspect the packet flow.

---

## Overview

The message packet structure is:

```
+-------------+------------+------------+------------------+--------------+-------------+
| 2 bytes TAG | 2 bytes ID | 1 byte SEQ | 2 bytes data LEN | n bytes DATA | 2 bytes CRC |
+-------------+------------+------------+------------------+--------------+-------------+
```

The TAG bytes are zero bytes.
The ID provides the message type.
The SEQ is a wrapping sequence number for the message
LENgth of the data precedes the data and is followed by a CRC for the packet.

Data is sent little-endian, i.e. not network byte-order.

Where a message will not fit into the maximum packet size before fragmentation, a split message container is used.

```
+------------+--------------+----------------+--------------+
| 2 bytes ID | 1 byte FRAGS | 1 byte FRAG_ID | n bytes DATA |
+------------+--------------+----------------+--------------+
```

The ID is the message type sent in fragments
FRAGS is the total number of fragments
FRAG_ID is the sequence number of the data in this fragment
DATA is the fragment data to be re-assembled

This forms the data component of the packet above.

## Client Session with a Server

As the protocol is connectionless, the message flow at session start up can happen out of order.
When a client starts a session with a server, it sends valid audio packets to the server port, to which the server will respond with the audio mix for that client.

The server on a new client connection will:

- Tell the client connection its ID, with a `CLIENT_ID (32, 0x2000)` message.
- Reset the connected client list with a `CONN_CLIENTS_LIST (24, 0x1800)` message.
- Determine if the client supports split messages, with a `REQ_SPLIT_MESSAGE_SUPPORT (34, 0x2200)` message.
- Request the details of the audio packets from the client with a `REQ_NETW_TRANSPORT_PROPS (21, 0x1500)` message,
- Request the number of jitter buffer value to use, with a `REQ_JITT_BUF_SIZE (11, 0x0B00)` message.
- Request the details of the channel info, with a `REQ_CHANNELS_INFOS (23, 0x1700)` message.
- Send the version and OS of the server, with a `VERSION_AND_OS (29, 0x1d00)` message.

This is defined in `CServer::OnNewConnection()`

The client on a new connection will:

- Send its channel info with a `CHANNELS_INFO (25, 0x1900)` message
- Request the list of connected clients with a `REQ_CONN_CLIENT_LIST (16, 0x1000)` message
- Set the server-side jitter buffer value with a `JITT_BUF_SIZE (10, 0x0a00)` message

This is defined in `CClient::OnNewConnection()`

At the end of the session, the client calls the `CLM_DISCONNECTION (1010, 0xf203)` message, until the server stops streaming audio to it.

A typical flow would be:

```
 Client                                     Server

  AUDIO --------------------------------->
      <------------------------------------ CLIENT_ID (32, 0x2000)
  ACK(CLIENT_ID) ------------------------>

      <------------------------------------ CONN_CLIENTS_LIST (24, 0x1800) (Reset to zero)
  ACK(CONN_CLIENTS_LIST) ---------------->

      <------------------------------------ REQ_SPLIT_MESSAGE_SUPPORT (34, 0x2200)
  SPLIT_MESS_SUPPORTED (35, 0x2300) --------->
  ACK(REQ_SPLIT_MESSAGE_SUPPORT) -------->
      <------------------------------------ ACK(SPLIT_MESS_SUPPORTED)

      <------------------------------------ REQ_NETW_TRANSPORT_PROPS (21, 0x1500)
  NETW_TRANSPORT_PROPS (20, 0x1400) --------->
  ACK(REQ_NETW_TRANSPORT_PROPS) --------->
      <------------------------------------ ACK(NETW_TRANSPORT_PROPS)

      <------------------------------------ REQ_JITT_BUF_SIZE (11, 0x0B00)
  JITT_BUF_SIZE (10, 0x0a00) ---------------->
  ACK(REQ_JITT_BUF_SIZE) ---------------->
      <------------------------------------ ACK(JITT_BUF_SIZE)

      <------------------------------------ REQ_CHANNELS_INFOS (23, 0x1700)
  CHANNEL_INFOS (25, 0x1900) ---------------->
  ACK(REQ_CHANNELS_INFOS) --------------->
      <------------------------------------ ACK(CHANNEL_INFOS)

(Optional welcome message)
      <------------------------------------ CHAT_TEXT (18, 0x1200)
  ACK(CHAT_TEXT) ------------------------>

      <------------------------------------ VERSION_AND_OS (29, 0x1d00)
  ACK(VERSION_AND_OS) ------------------->

  CHANNEL_INFOS (25, 0x1900) ---------------->
      <------------------------------------ ACK(CHANNEL_INFOS)

      <------------------------------------ RECORDER_STATE (33, 0x2100)
  ACK(RECORDER_STATE) ------------------->


  REQ_CONNECTED_CLIENTS_LIST (16, 0x1000) ---->
      <------------------------------------ ACK(REQ_CONNECTED_CLIENTS_LIST)

  REQ_CHANNEL_LEVEL_LIST (28, 0x1c00) -------->

  JITT_BUF_SIZE (10, 0x0a00) ---------------->
      <------------------------------------ JITT_BUF_SIZE (10, 0x0a00)
      <------------------------------------ ACK(JITT_BUF_SIZE)
  ACK(JITT_BUF_SIZE) -------------------->

      <------------------------------------ CONN_CLIENTS_LIST (24, 0x1800)
  ACK(CONN_CLIENTS_LIST) ---------------->

  NETW_TRANSPORT_PROPS (20, 0x1400) --------->
      <------------------------------------ CONN_CLIENTS_LIST (24, 0x1800)
      <------------------------------------ ACK(NETW_TRANSPORT_PROPS)
  ACK(CONN_CLIENTS_LIST) ---------------->
```

## General Streaming Messages

During streaming, some control messages are used.
Some typical messages could be:

```
 Client                                     Server

      <------------------------------------ CLM_CHANNEL_LEVEL_LIST (1015, 0xf703)

  CHANNEL_GAIN (13, 0x0d00) ----------------->

  CLM_PING_MS (1001, 0xe903) ------------------>

      <------------------------------------ ACK(CHANNEL_GAIN)

      <------------------------------------ CLM_PING_MS (1001, 0xe903)

  MUTE_STATE_CHANGED (31, 0x1f00) ----------->
      <------------------------------------ ACK(MUTE_STATE_CHANGED)

  NETW_TRANSPORT_PROPS (20, 0x1400) --------->
      <------------------------------------ ACK(NETW_TRANSPORT_PROPS) - Reset audio packet sequencing on change

  CHANNEL_PAN (30, 0x1e00) ------------------>
      <------------------------------------ ACK(CHANNEL_PAN)
```

---

## Audio Packet Structure

The OPUS codec is used to compress the audio over the network and the packets are documented [here](https://datatracker.ietf.org/doc/html/rfc6716).

Jamulus uses a custom OPUS encoder / decoder, giving some different frame sizes, but always uses a 48kHz sample rate. OPUS and OPUS64 codecs are the only supported options currently.

The packet size will vary based on:

- Stereo vs mono
- Buffer size (64/128/256 samples)
- Use of frame sequence number (from v3.6.0 onwards)

These values are wrapped up into the `NETW_TRANSPORT_PROPS` messages, which the client sends to the server to tell it which values to use.

Both client and server use a jitter buffer for received audio data to prevent audio drop-out. This is configurable.

---

## Poly-in session extension (version 1)

This section is normative for the optional extension called **Poly-in** in
[POLY_IN.md](POLY_IN.md). The implementation's client UI calls the same feature
**Poly-in routing**.

The extension transports several independently mixable upstream sources through
one ordinary Jamulus client/server session. It adds reliable control messages
and a separate upstream audio-datagram format. It does **not** alter legacy
audio packets, the existing return transport, or legacy client/server startup.

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHOULD**, **SHOULD NOT**
and **MAY** in this section are to be interpreted as normative requirements.

### Interoperability rules

- A Poly-in client MUST begin as an ordinary legacy client and MUST continue
  ordinary legacy upload until it has received `POLY_IN_ACCEPT`.
- Selecting Poly-in MUST establish a two-channel ordinary session transport
  before connection. Unsupported, refused or timed-out negotiation therefore
  continues as an ordinary stereo legacy session.
- A server MUST NOT probe or reinterpret a legacy client. An old client never
  sends `REQ_POLY_IN_CAPS` and therefore remains legacy.
- A generic acknowledgement of an unknown reliable message is **not** evidence
  of Poly-in support. Only `POLY_IN_CAPS` is affirmative capability
  evidence.
- `POLY_IN_CAPS` is meaningful only after the existing split-message
  prerequisite has completed. A supporting server sends it only when it is
  ready to receive a split-capable configuration.
- A client MUST begin the timeout for each control stage only after the
  corresponding logical request has actually left the ACK-gated reliable
  message queue. Queuing the request is not sufficient.
- A timeout, missing semantic capability reply or rejection MUST leave the
  legacy session usable. A client MUST NOT transmit Poly-in UDP audio before
  receiving a valid `POLY_IN_ACCEPT`.
- After `POLY_IN_ACCEPT` is acknowledged, a server MUST bound how long the
  hidden prepared map may remain without first audio. Expiry MUST invalidate
  that generation and release its hidden sources and ingress state while
  preserving the ordinary legacy session.
- Once Poly-in audio is active, both sides MUST reject or ignore data for a
  stale configuration generation. Changing source routing requires a new
  connection in version 1.

### Message identifiers

The reliable-message IDs are defined in `src/protocol.h`.

| ID | Message | Direction | Meaning |
| ---: | --- | --- | --- |
| 37 | `REQ_POLY_IN_CAPS` | client → server | Request semantic Poly-in capability. Body is empty. |
| 38 | `POLY_IN_CAPS` | server → client | Extension version and maximum source rows. |
| 39 | `POLY_IN_CONFIG` | client → server | Versioned source descriptors; may use existing split-message support. |
| 40 | `POLY_IN_ACCEPT` | server → client | Accepted generation and local-source-key to visible-fader map. |
| 41 | `POLY_IN_REJECT` | server → client | Version and rejection reason. |
| 42 | `POLY_IN_ACTIVE` | server → client | Confirmation that first valid Poly-in audio committed the map. |

The bodies below use unsigned integer fields. Every multi-byte integer in this
extension is big-endian/network byte order. `u8`, `u16` and `u32` mean an
unsigned 8-, 16- and 32-bit integer respectively.

### Reliable control bodies

#### `REQ_POLY_IN_CAPS`

The body is empty.

A server receives this request only after ordinary client connection startup.
It MUST send `POLY_IN_CAPS` only if the session is connected and the
existing split-message capability is already known. A server that cannot make
that promise MAY remain silent; a client then falls back to legacy operation.

#### `POLY_IN_CAPS`

```text
0  u8  version       1
1  u8  max rows      1..64
```

A version-1 receiver accepts this body only when it has exactly two bytes,
`version == 1`, and `max rows` is in `1..64`.

#### `POLY_IN_CONFIG`

```text
0  u8  version       1
1  u8  session flags bit 0 = Raw; bits 1..7 = 0
2  u8  source count  1..64
3  repeated source descriptor:
     u8   local source key
     u8   audio channels         1 = mono, 2 = stereo
     u8   codec                  CT_OPUS or CT_OPUS64
     u8   Raw                    0 or 1; MUST equal session flag bit 0
     u16  payload bytes
     u16  instrument
     u8   UTF-8 tag byte length  1..63
     u8[] UTF-8 tag
```

The body has no padding. Each descriptor consumes `9 + tag length` bytes.
`source count` descriptors MUST consume the body exactly; trailing bytes are
invalid.

A version-1 implementation MUST reject a configuration unless all of the
following hold:

- source count is in `1..64`;
- every local key is non-zero and unique within the configuration;
- every source has one or two audio channels;
- codec is `CT_OPUS` or `CT_OPUS64`;
- all sources use the same codec and Raw policy;
- each per-source Raw byte agrees with the session Raw bit;
- tag bytes decode as a non-empty UTF-8 tag of at most 63 bytes, and tags are
  unique after the implementation's trimmed tag comparison;
- payload length is one of the exact fixed sizes permitted by codec, channel
  count and Raw policy; and
- sufficient source and physical-session capacity is available at the server.

The accepted compressed payload sizes are fixed CBR Jamulus values:

| Codec/frame size | Mono payload bytes | Stereo payload bytes |
| --- | ---: | ---: |
| `CT_OPUS` / 128 samples | 25, 45, 82 | 47, 71, 165 |
| `CT_OPUS64` / 64 samples | 12, 22, 36 | 24, 35, 73 |

For Raw, payload bytes MUST be `sizeof(int16_t) × audio channels × frame
samples`: 256/512 bytes for mono/stereo `CT_OPUS`, and 128/256 bytes for
mono/stereo `CT_OPUS64`.

The instrument field is an ordinary Jamulus instrument value. It does not make
a new participant identity: the server combines parent session profile
information with the source tag and instrument when creating a visible source.

#### `POLY_IN_ACCEPT`

```text
0  u8  version       1
1  u16 generation    non-zero
3  u8  source count  1..64
4  repeated accepted source:
     u8   local source key
     u16  visible fader ID
```

The source count and key set MUST match the accepted configuration. Fader IDs
are ordinary global visible mixer IDs and MUST be in the implementation's
normal mixer range. A receiver rejects a body whose exact length is not
`4 + 3 × source count`, whose generation is zero, or whose local keys are
zero/duplicate.

`POLY_IN_ACCEPT` reserves the source map and gives the client permission to
start sending Poly-in upstream frames for `generation`. It does not yet mean
that other clients can see the sources. The version-1 server starts a
five-second prepared-state expiry after the complete logical acceptance has
been acknowledged. First valid audio cancels that expiry.

#### `POLY_IN_REJECT`

```text
0  u8  version  1
1  u8  reason
```

Defined version-1 reasons are:

| Value | Name | Meaning |
| ---: | --- | --- |
| 1 | malformed | Configuration or state was invalid. |
| 2 | capacity | Visible-source or physical-session capacity was insufficient. |
| 3 | unsupported | Requested feature/policy is not available. |
| 4 | split message not ready | The required existing split-message support was not complete. |
| 5 | invalid session state | The session was not in a state that permits configuration. |
| 6 | stereo return required | The physical session had not negotiated a two-channel return profile. |

A client receiving a syntactically valid reject MUST stop the pending Poly-in
negotiation and retain/return to legacy upload for that connection.

#### `POLY_IN_ACTIVE`

```text
0  u8  version     1
1  u16 generation  non-zero
```

The server sends this only after receiving the first valid Poly-in audio frame
for an accepted generation and atomically committing the visible source map.
It is a confirmation, not permission to begin transmitting: permission was
already given by `POLY_IN_ACCEPT`.

### Negotiation and promotion sequence

```text
client                                            server
  | ordinary legacy connection/startup              |
  |------------------------------------------------>|
  | REQ_POLY_IN_CAPS                            |
  |------------------------------------------------>|
  |                         POLY_IN_CAPS        |
  |<------------------------------------------------|
  | POLY_IN_CONFIG (may be split)               |
  |------------------------------------------------>|
  |                         POLY_IN_ACCEPT      |
  |<------------------------------------------------|
  | Poly-in UDP fragments for accepted generation  |
  |------------------------------------------------>|
  |                         atomically promote map  |
  |                         POLY_IN_ACTIVE      |
  |<------------------------------------------------|
```

Before `POLY_IN_ACCEPT`, the client MUST send only ordinary legacy upload
audio. The server keeps the normal temporary legacy visible source while the
Poly-in configuration is prepared. On the first valid Poly-in fragment for
the accepted generation, it atomically retires that temporary source, exposes
all reserved sources, publishes one connected-client-list update and then
sends `POLY_IN_ACTIVE` from the server QObject/protocol-owning thread.

If no valid accepted-generation frame arrives before the prepared-state expiry,
the server MUST release the hidden source bank, clear ingress/generation state
and return the session to legacy operation. A disconnect while the map is
prepared MUST do the same as part of full session teardown. A timeout or
explicit disconnect while active MUST retire every source belonging to the
physical session, clear Poly-in reassembly/generation state, and reset the
reliable protocol queue before the session slot can be reused.

### Poly-in upstream UDP datagrams

A Poly-in upstream datagram is not a reliable protocol message. Its non-zero
magic prevents it being mistaken for a protocol frame whose tag begins with two
zero bytes.

```text
0   u16  magic       0x5049 ("PI")
2   u8   version     1
3   u8   flags       bit 0 = Raw; bits 1..7 = 0
4   u16  generation  accepted configuration generation
6   u32  sequence    monotonically increasing session-frame sequence
10  u8   fragment    zero-based fragment index
11  u8   fragments   total fragments, 1..32
12  u8   records     records carried by this fragment
13  u8   reserved    0
14  repeated record:
      u8   local source key
      u16  payload bytes
      u8[] encoded or Raw payload
```

All source records produced for one codec-frame boundary MUST use the same
session `sequence`, including records sent in different UDP fragments. The
sequence is a session sequence, not one sequence per source.

A datagram MUST satisfy all of these conditions before it changes ingress
state:

- length is at least the 14-byte header and at most the 1200-byte application
  limit;
- magic and version match exactly;
- no reserved or unknown flag bit is set;
- generation equals the currently prepared/active generation;
- `fragments` is in `1..32` and `fragment < fragments`;
- reserved header byte is zero;
- record data exactly fills the datagram after the header;
- every local key is known in the accepted descriptor map and occurs at most
  once in the logical sequence;
- every payload length equals the descriptor's negotiated fixed payload size;
- duplicate fragment indexes, incompatible fragment counts and sequences
  outside the bounded receive window are rejected; and
- a malformed, stale or unconfigured datagram leaves playout state unchanged.

Records MUST NOT span fragments. The version-1 application payload limit is
1200 bytes. The largest supported record is Raw stereo at 128 samples:

```text
record header (1 + 2 bytes) + 2 channels × 128 samples × 2 bytes = 515 bytes
```

Two such records plus the 14-byte datagram header occupy 1044 bytes. Therefore
two worst-case records fit one fragment and 64 worst-case sources require at
most 32 fragments.

### Reassembly, loss and jitter semantics

The server uses a fixed, session-level logical-frame reassembly ring configured
only during negotiation/reconnect. A ring slot is indexed by shared session
sequence and records which fragments and source payloads have arrived.

- A logical frame becomes usable when at least one valid fragment for that
  sequence arrives.
- Loss of a fragment makes only its records absent. Valid source records in
  other fragments for the same sequence remain usable.
- Missing compressed source payloads use the existing source decoder's packet
  loss concealment; missing Raw payloads produce silence.
- The ingress jitter estimator observes one arrival per logical session frame,
  not one per UDP fragment or source. Source/fragment loss alone does not count
  as a timing failure when another fragment for the frame arrived.
- Reassembly is bounded and wrap-safe. Too-old, too-far-ahead and discontinuous
  sequences are discarded or explicitly re-anchored without replaying stale
  audio.

There is no per-source network jitter buffer or return stream. Source-local
loss handling and session-level timing are deliberate.

### Return stream and cadence

The downstream stream remains the ordinary transport negotiated at connection
start. A Poly-in client negotiates that physical session as stereo before
capability discovery, and a server MUST reject `POLY_IN_CONFIG` on a mono
session. There is exactly one stereo return encoder and return stream per
Poly-in session, regardless of number of sources.

The server MUST send return packets at the negotiated return codec cadence. In
particular, a 128-sample server callback feeding a 64-sample `CT_OPUS64` return
profile MUST encode and send two non-overlapping return packets per callback.
This applies to Poly-in and legacy sessions alike; sending one packet would
supply half the required return cadence.

### Implementation and validation requirements

- Parsing, packetisation and reassembly MUST use bounded storage derived from
  negotiated limits. A socket datagram or audio callback MUST NOT cause
  unbounded allocation or source-map growth.
- The high-priority socket worker MAY write accepted data to preallocated
  ingress storage, but MUST NOT start Qt timers or directly perform reliable
  protocol/UI promotion work. Promotion and protocol messages run in the
  owning server QObject thread.
- A client MUST track every accepted visible fader ID as an owned source. All
  own-source handling—own-first ordering, gain/mute behaviour, auto-level
  exclusion and local monitoring—MUST apply to the complete owned set.
- The server MUST apply decode, meter, fade and recorder lifecycle per visible
  source, then mix/encode/transmit once per physical session.
