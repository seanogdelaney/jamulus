# Advanced multi-source: Small Network Buffers return-cadence fix

Apply this patch after:

1. `jamulus_multisource_session.patch`
2. `jamulus_multisource_ingress_autojitter.patch`
3. `jamulus_multisource_threading_fix.patch`

## Bug fixed

With **Small Network Buffers** selected, an Advanced client negotiates a
64-sample `CT_OPUS64` return profile. A default Jamulus server timer runs at
128 samples. The multi-source server refactor mixed the full 128-sample block,
but encoded and transmitted only its first 64 samples. The client therefore
received one return packet per 128 server samples while expecting one per 64
samples: exactly half the required cadence.

The client-side return jitter buffer interprets this deterministic packet
starvation as severe loss. Its automatic local buffer rises rapidly and the
jitter indicator turns red. `--fastupdate` masks the defect because it changes
the server timer to 64 samples, where one packet per callback is correct.

## Change

`CServer::MixEncodeTransmitData()` now explicitly sends the number of return
packets required by the server/output-frame relationship:

| Server callback | Return codec | Packets per callback |
| --- | --- | --- |
| 128 samples | `CT_OPUS` (128) | 1 |
| 128 samples | `CT_OPUS64` (64) | 2 |
| 64 samples | `CT_OPUS` (128) | 0 or 1 after existing accumulation |
| 64 samples | `CT_OPUS64` (64) | 1 |

For a 128-sample server to `CT_OPUS64`, the two packets encode non-overlapping
first and second 64-sample halves of the mixed PCM block. The same loop is used
for Raw return streams, preserving the existing `PrepAndSendPacket()` sequence
counter and output conversion path.

The helper is intentionally codec/frame based, not Advanced-only: every
session using a `CT_OPUS64` return profile requires the same cadence.

## Expected result

On a normal (128-sample) server, enabling Small Network Buffers should no
longer cause the Advanced client’s local auto-jitter setting to climb solely
because the server is delivering the return stream at half rate. The actual
jitter target should then reflect network/audio scheduling conditions rather
than this deterministic cadence defect.

## Validation performed

Applied the diff to a fresh tree containing the three prerequisite patches,
then ran the deterministic wire/state harness, including the new four-case
server/output cadence test:

```sh
g++ -std=c++17 -Wall -Wextra -Werror tests/multisourcewire_test.cpp \
  src/multisourcewire.cpp -o /tmp/multisourcewire_test
/tmp/multisourcewire_test
```

Result: `multisourcewire tests: PASS`.

A full Qt/qmake build was not available in this environment because qmake and
Qt development headers/libraries are not installed.
