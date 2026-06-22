# Jambox Jamulus startup trace

This checkout contains diagnostic-only startup tracing under `JAMULUS_STARTUP_TRACE`.
The trace path is fixed-buffer/in-memory during reproduction and dumped only after
the GUI event loop exits cleanly.

## Build

```sh
cd /home/sdel/code/jamulus
make -j"$(nproc)" release
```

The host binary is:

```text
/home/sdel/code/jamulus/jamulus
```

## Trace Capture

The helper starts JACK, a no-GUI mic client, and a GUI instrument client, then uses
`xdotool` to close the GUI. Set `CONNECT_SERVER` and `JACK_DEVICE` to match the
target arrangement.

```sh
cd /home/sdel/code/jamulus
CONNECT_SERVER=sdel.ddns.us JACK_DEVICE=hw:1 ./.codex-logs/run-host-startup-trace.sh normal
CONNECT_SERVER=sdel.ddns.us JACK_DEVICE=hw:1 ./.codex-logs/run-host-startup-trace.sh settings
```

Each run prints a directory under `/tmp/jamulus-codex-logs/ubuntu-host-*`.
The GUI fixed-buffer dump is `gui.trace`; process logs are `gui.log`, `mic.log`,
`server.log`, and `jack.log`.

Set an explicit trace file for manual runs:

```sh
JAMULUS_STARTUP_TRACE_FILE=/tmp/jamulus-codex-logs/gui.trace ./jamulus ...
```

## Tags

- `GUI_CLIENT_DLG_CTOR_ENTER` / `GUI_CLIENT_DLG_CTOR_EXIT`: main creates the GUI dialog.
- `DLG_CTOR_ENTER` / `DLG_CTOR_EXIT`: dialog construction boundaries.
- `DLG_LIST_SIGNAL_CONNECT_BEGIN` / `DLG_LIST_SIGNAL_CONNECTED`: connected-client list receiver installation.
- `STARTUP_CONNECT_*`: startup connection scheduling and execution.
- `CLIENT_START_*`, `CLIENT_STOP_*`, `SOUND_START_*`, `SOUND_REINIT_*`: client and sound lifecycle.
- `CLIENT_ID_*`, `CLIENT_CHANNELS_CLEAR*`, `CLIENT_LIST_RX`, `CLIENT_LIST_EMIT`: protocol state and channel mapping.
- `DLG_LIST_SLOT_ENTER`, `DLG_LIST_SLOT_EXIT`, `MIXER_APPLY_BEGIN`, `MIXER_APPLY_END`, `MIXER_HIDE_ALL_*`: GUI mixer path.
- `SETTINGS_OPEN`, `SETTINGS_*`, `STATUS_SNAPSHOT`: Settings path and first status samples.

Trace columns are:

```text
seq usec thread tag a b c
```

The meaning of integer payloads is local to each tag. For the main mixer/list path:
list size is payload `a`, running/boolean state is usually `b`, and visible fader
count is usually `c`.

## Interpretation

| Observation | Interpretation |
|---|---|
| `CLIENT_LIST_EMIT` precedes `DLG_LIST_SIGNAL_CONNECTED` | The initial list can be missed. |
| `DLG_LIST_SLOT_ENTER` does not follow a post-connection `CLIENT_LIST_EMIT` | Investigate signal delivery or receiver lifetime. |
| `MIXER_APPLY_END` has non-zero size and zero visible count | Investigate mixer rebuild/visibility logic. |
| A later `MIXER_HIDE_ALL_*` follows a successful apply | The later reset is the real cause. |
| Opening Settings emits profile/audio changes before recovery | Investigate settings apply or sound reinit side effects. |
| Recovery happens only after a later `CLIENT_LIST_RX` | Investigate why the first list contents or ordering differ. |

The diagnostics do not intentionally change startup order, Qt connection types,
network/audio decisions, or saved settings. This checkout's base already contains
the experimental GUI `QTimer::singleShot(0)` startup-connect deferral; this trace
does not add another startup ordering workaround.
