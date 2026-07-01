# Advanced multi-source: thread-safe promotion and jitter reporting

Apply this patch after:

1. `jamulus_multisource_session.patch`
2. `jamulus_multisource_ingress_autojitter.patch`

## Bug fixed

The first accepted Advanced audio packet is handled by `CHighPrioSocket`'s
receive thread.  The prior patches promoted the session and called
`CChannel::CreateJitBufMes()` directly in that thread.  That enters
`CProtocol::SendMessage()`, which starts `CProtocol::TimerSendMess`.  The
timer belongs to the `CServer` QObject thread, hence Qt can report:

```
QObject::startTimer: Timers cannot be started from another thread
```

The protocol packet carrying the new two-frame Advanced ingress target can
therefore fail to leave the server.  The client keeps its previous legacy
server-buffer value (normally ten frames), so `EstimatedOverallDelay()` still
shows roughly 45 ms despite the Advanced ingress ring itself being configured
for two frames.

## Change

The socket worker now only writes the first Advanced packet to the already
preallocated ingress ring and queues a `CCustomEvent` to `CServer`.

`CServer::customEvent()` performs the following in its owning QObject thread:

- promotes reserved source faders;
- retires the temporary legacy fader;
- emits recorder/client-list notifications;
- sends the initial Advanced server-jitter target through the established
  protocol path.

Subsequent auto-jitter target changes are likewise queued before calling the
protocol.  No `CProtocol` message is created directly by either the socket
worker or the Advanced timing path.

The first session frame remains in the preallocated ring while waiting for the
queued event.  A disconnect or rejected/replaced configuration makes the event
a no-op by state/generation checks.

## Expected result

With client and server automatic jitter enabled, the Advanced client should
receive a server target of two codec frames shortly after source-fader
promotion.  The delay display should therefore stop retaining the legacy
10-frame server value.  This corrects the status/reporting path; observed
end-to-end latency still includes the normal client return buffer and hardware
I/O buffering.

## Validation performed

- Generated as a unified diff against the tree with both earlier multi-source
  patches applied.
- Applied the diff to a fresh copy of that exact predecessor tree.
- Re-ran the standalone deterministic multi-source wire/state harness:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/multisourcewire_test.cpp \
  src/multisourcewire.cpp -o /tmp/multisourcewire_test
/tmp/multisourcewire_test
```

Result: `multisourcewire tests: PASS`.

A full Qt/qmake build was not possible in this environment because qmake and
Qt development headers are unavailable.
