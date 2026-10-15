# Stage 1 executable stock baseline tests

These are test-only; **no production code is changed**. Run from patched source root:

```sh
python3 tests/stock_stage01/run_buffer_real_stock.py --golden > L01.csv
python3 tests/stock_stage01/run_pipeline_fixture.py --stock . > L02_L04.txt
python3 tests/stock_stage01/inspect_stock_contracts.py
```

The pipeline fixture extracts exact unmodified `CServer::DecodeReceiveData` and
`CServer::MixEncodeTransmitData` method bodies from `src/server.cpp` and compiles
them with vendored Opus against test-only Qt/channel/socket stand-ins. Asserts
real-method raw/Opus/PLC, conversion cadence, gain/pan, fade, clipping,
`--delaypan` OFF/ON history, and encoded packets. It does **not** exercise
`CChannel`/Qt networking, GUI, recorder, or real client lifecycle.

A fingerprint from this fixture depends on compiler/CPU/Opus build choices.
`--golden` checks recorded fingerprints for two test environments only; use
**identical compiler/options/input against pristine stock and R01**, and
compare emitted text byte-for-byte for portable regression parity. The
`verify_stage01.py` companion automates that and checks L01 (requires CMake,
C++17 compiler, Python3, Git, vendored Opus). Example:

```sh
python3 tests/stock_stage01/verify_stage01.py \
  --master /path/to/jamulus-polyin-immutable-master-v4.zip \
  --handoff /path/to/stage-01-output.zip \
  --output /path/to/evidence
```

The tester independently compiled and linked the full unmodified Jamulus
GUI/client/server app on ARM64 macOS. That is **build evidence**, not Qt runtime
integration evidence. L05 recorder/meter/lifecycle and actual legacy-client
interaction require future feature integration: Stage 4 owns live audio/meter/
recorder mapping; Stage 5 owns full Qt and legacy-client system tests. OF-01
feature-adapter parity becomes runnable in Stage 2 when an adapter exists.
