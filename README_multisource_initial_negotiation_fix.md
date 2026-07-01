# Multi-source deterministic initial-negotiation follow-on

## Applies after

Apply this patch after, in order:

1. `jamulus_multisource_session.patch`
2. `jamulus_multisource_ingress_autojitter.patch`
3. `jamulus_multisource_threading_fix.patch`
4. `jamulus_multisource_small_network_buffers.patch`

It is a follow-on diff, not a replacement for any of them.

## Purpose

A new Advanced client and new server should promote on the first connection,
not only after a lucky reconnect. The prior path could start a timeout while
its request was still behind normal startup messages in the ACK-gated protocol
queue. It also had an implicit split-message ordering dependency and no precise
client-visible distinction between rejection, waiting for a first frame, and
successful source-map promotion.

This patch makes startup a narrow explicit state machine:

```text
legacy
  -> waiting for split capability
  -> capability request physically sent
  -> semantic MULTISOURCE_CAPS reply
  -> configuration physically sent
  -> MULTISOURCE_ACCEPT (generation)
  -> first generation-tagged Advanced frame
  -> MULTISOURCE_ACTIVE (generation)
```

Deadlines start when the relevant logical reliable message first leaves
`CProtocol`'s FIFO, not when code merely queues it. A generic ACK is still not
capability evidence. `MULTISOURCE_CAPS` is now a server promise that split
message support is complete and the server is ready to accept a source map.
`MULTISOURCE_ACTIVE` is an acknowledgement sent by the server only after the
first Advanced frame atomically replaces the temporary legacy fader.

## Deliberate configuration policy

There is no live Advanced reconfiguration in this version. Once `CClient::Start`
begins an Advanced connection, the routing table, input mode, codec quality/Raw
choice and Small Network Buffers option are locked until `Stop`. The settings UI
explains: **Advanced routing is fixed for this connection. Disconnect to change
it.** This keeps the configured source descriptors, client encoders and server
fader map immutable for one session.

A first-patch server without `MULTISOURCE_ACTIVE` remains usable: after the
client has sent Advanced UDP it keeps transmitting rather than falling back on
the confirmation timeout, but its status says activation was not confirmed.

## Changed areas

- `src/multisourcewire.*`: thread-safe negotiation state machine with separate
  queued/sent/accepted/active stages.
- `src/protocol.*`, `src/channel.*`, `src/multisource.*`: logical reliable-send
  notification and `MULTISOURCE_ACTIVE` protocol message.
- `src/server.cpp`: only advertise capabilities after split setup is complete;
  send activation confirmation from the server QObject thread.
- `src/client.*`, `src/clientsettingsdlg.cpp`: start deadlines at real send
  boundaries, expose exact stage/failure status, and lock configuration after
  startup.
- `docs/JAMULUS_PROTOCOL.md` and `README_multisource_session.md`: protocol and
  scope documentation.
- `tests/multisourcewire_test.cpp`: deterministic negotiation sequencing and
  timeout compatibility coverage.

## Validation

Run the focused non-Qt harness:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/multisourcewire_test.cpp \
  src/multisourcewire.cpp -o /tmp/multisourcewire_test
/tmp/multisourcewire_test
```

Expected output:

```text
multisourcewire tests: PASS
```

A full qmake build is still required on a machine with Qt development headers,
qmake and the normal Jamulus dependencies.
