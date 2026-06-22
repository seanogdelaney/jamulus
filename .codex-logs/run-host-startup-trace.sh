#!/usr/bin/env bash
set -euo pipefail

ROOT="/home/sdel/code/jamulus"
JAMULUS="$ROOT/jamulus"
JAMBOX="/home/sdel/scripts/jambox"
STAMP="$(date +%Y%m%d-%H%M%S)"
BASE="/tmp/jamulus-codex-logs/ubuntu-host-$STAMP"
MODE="${1:-normal}"
CONNECT_SERVER="${CONNECT_SERVER:-127.0.0.1}"

mkdir -p "$BASE"
cp "$JAMBOX/appliance/config/jamulus/instrument-mono.ini" "$BASE/instrument-mono.ini"
cp "$JAMBOX/appliance/config/jamulus/mic.ini" "$BASE/mic.ini"
printf 'jambox-local-rpc-secret-2026\n' >"$BASE/jsonrpc.secret"

cleanup() {
  set +e
  for name in gui mic server jack; do
    if [[ -s "$BASE/$name.pid" ]]; then
      kill "$(cat "$BASE/$name.pid")" 2>/dev/null || true
    fi
  done
  wait 2>/dev/null || true
}
trap cleanup EXIT

echo "Trace directory: $BASE"
echo "Binary: $JAMULUS"
echo "Mode: $MODE"
echo "Connect server: $CONNECT_SERVER"
echo "Starting local Jamulus server, no-GUI mic client, and GUI instrument client."

if ! jack_lsp >/dev/null 2>&1; then
  if [[ -n "${JACK_DEVICE:-}" ]]; then
    echo "No running JACK detected; starting ALSA JACK for this trace: $JACK_DEVICE"
    jackd -R -P70 -dalsa "-d${JACK_DEVICE}" "-r${JACK_RATE:-48000}" "-p${JACK_PERIOD:-64}" "-n${JACK_NPERIODS:-2}" >"$BASE/jack.log" 2>&1 &
  else
    echo "No running JACK detected; starting dummy JACK for this trace."
    jackd -R -P70 -d dummy -r48000 -p64 -C 4 -P 2 >"$BASE/jack.log" 2>&1 &
  fi
  echo "$!" >"$BASE/jack.pid"
  for _ in $(seq 1 30); do
    jack_lsp >/dev/null 2>&1 && break
    sleep 0.5
  done
else
  echo "Using existing JACK server."
  jack_lsp >"$BASE/jack-ports-before.log" 2>&1 || true
fi

if [[ "$CONNECT_SERVER" == "127.0.0.1" || "$CONNECT_SERVER" == "localhost" ]]; then
  QT_FORCE_STDERR_LOGGING=1 QT_MESSAGE_PATTERN='%{message}' \
    "$JAMULUS" --server --nogui --numchannels 10 \
    >"$BASE/server.log" 2>&1 &
  echo "$!" >"$BASE/server.pid"
  sleep 1
else
  : >"$BASE/server.log"
fi

QT_FORCE_STDERR_LOGGING=1 QT_MESSAGE_PATTERN='%{message}' \
  "$JAMULUS" --clientname jamulus-mic --port 22135 \
  --jsonrpcport 22137 --jsonrpcsecretfile "$BASE/jsonrpc.secret" --jsonrpcbindip 127.0.0.1 \
  --nogui --nojackconnect --inifile "$BASE/mic.ini" --connect "$CONNECT_SERVER" \
  >"$BASE/mic.log" 2>&1 &
echo "$!" >"$BASE/mic.pid"
sleep 1

JAMULUS_STARTUP_TRACE_FILE="$BASE/gui.trace" \
  "$JAMULUS" --clientname jamulus-instrument --port 22134 \
  --jsonrpcport 22136 --jsonrpcsecretfile "$BASE/jsonrpc.secret" --jsonrpcbindip 127.0.0.1 \
  --nojackconnect --inifile "$BASE/instrument-mono.ini" --connect "$CONNECT_SERVER" \
  >"$BASE/gui.log" 2>&1 &
echo "$!" >"$BASE/gui.pid"
gui_pid="$(cat "$BASE/gui.pid")"

if command -v xdotool >/dev/null 2>&1; then
  main_window="$(timeout 20s xdotool search --sync --pid "$gui_pid" --onlyvisible | head -n1 || true)"
  if [[ -n "$main_window" ]]; then
    xdotool windowactivate "$main_window" >/dev/null 2>&1 || true
    if [[ "$MODE" == "settings" ]]; then
      sleep 5
      xdotool windowactivate --sync "$main_window" >/dev/null 2>&1 || true
      xdotool key --window "$main_window" ctrl+s
      sleep 5
      xdotool windowactivate "$main_window" >/dev/null 2>&1 || true
      xdotool key --window "$main_window" ctrl+q
    else
      sleep 15
      xdotool key --window "$main_window" ctrl+q
    fi
  else
    echo "WARN: xdotool did not find Jamulus GUI window for pid $gui_pid" >"$BASE/xdotool.warn"
    sleep 20
    kill "$gui_pid" 2>/dev/null || true
  fi
else
  echo "WARN: xdotool not installed" >"$BASE/xdotool.warn"
  sleep 20
  kill "$gui_pid" 2>/dev/null || true
fi

set +e
wait "$gui_pid"
gui_rc=$?
set -e

cleanup
trap - EXIT

echo "GUI exit code: $gui_rc"
echo "Trace directory: $BASE"
echo "GUI trace lines: $(wc -l <"$BASE/gui.trace" 2>/dev/null || printf 0)"
echo "Key trace preview:"
grep -E 'GUI_CLIENT_DLG_CTOR|DLG_CTOR|STARTUP_CONNECT|CLIENT_START|CLIENT_ID|CLIENT_LIST|DLG_LIST|MIXER_APPLY|MIXER_HIDE|SETTINGS|SOUND_REINIT|STATUS_SNAPSHOT' \
  "$BASE/gui.trace" | sed -n '1,120p' || true
