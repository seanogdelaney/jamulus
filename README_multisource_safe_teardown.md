# Advanced-session safe teardown

**Applies after:**

1. `jamulus_multisource_session.patch`
2. `jamulus_multisource_ingress_autojitter.patch`
3. `jamulus_multisource_threading_fix.patch`
4. `jamulus_multisource_small_network_buffers.patch`
5. `jamulus_multisource_initial_negotiation.patch`

## Problem

The legacy server tear-down path used `CChannel::Disconnect()`.  That only
set a one-sample timeout; the actual release happened later from
`CChannel::GetData()`.  An active Advanced session does not consume its
legacy `CChannel::SockBuf`, because its input instead comes from the
session-level `SessionIngress` reassembly buffer.  Consequently a normal
client disconnect could leave the same server slot live: old source faders,
endpoint address, prepared/active generation, ingress frames and reliable
protocol queue remained associated with the next connection from that UDP
address.

## Change

`CServer::OnCLDisconnection()` now retires the whole physical session
immediately, while the server mutex already held by connectionless protocol
parsing serializes it against the high-priority socket receive path.  It:

- removes every visible source owned by the session and emits recorder
  disconnects for active sources;
- clears the Advanced generation, ingress ring and queued promotion state;
- removes the endpoint from the address-sorted session lookup pool;
- resets the `CChannel` transport, socket/return conversion buffers,
  participant identity and reliable protocol queue/timer;
- sends one normal connected-client-list update to remaining sessions.

`CProtocol::Reset()` now stops its retransmission timer before clearing the
queue, preventing a retired slot from later retransmitting a stale control
message after reuse.

As a lost-disconnect recovery path, the server also now decrements the
ordinary sample-based timeout once per *active Advanced session* per server
tick.  A timeout retires that session through the same `FreeChannel()` path.
Legacy sessions retain their existing `GetData()`-based timeout behavior.

## Expected behaviour

Disconnecting an Advanced client removes all of its source faders immediately.
A reconnect from the same client/socket begins a new legacy startup session,
then performs the normal Advanced capability/configuration/promotion sequence.
It cannot inherit an old source map or ingress backlog.

## Validation performed

- Applied this follow-on patch to a fresh tree with all five prerequisite
  patches applied.
- Re-applied that complete six-patch stack to a second fresh extraction of the
  original supplied archive, with no rejects.
- Built and ran the existing deterministic multiplexing harness:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/multisourcewire_test.cpp \
  src/multisourcewire.cpp -o /tmp/multisourcewire_safe_teardown_test
/tmp/multisourcewire_safe_teardown_test
```

The harness covers the pure wire/reassembly/negotiation components and passed.
The full Qt/qmake application build was not run here because `qmake` and Qt
C++ development headers are unavailable in this environment.
