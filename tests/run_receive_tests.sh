#!/bin/sh
# Qt-free smoke and deterministic transport regression tests.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/jamulus-receive-test.XXXXXXXX")
trap 'rm -rf "$OUT"' EXIT HUP INT TERM
CXX=${CXX:-c++}
CXXFLAGS=${CXXFLAGS:--O2 -std=c++17 -Wall -Wextra -pedantic}
# Intentional shell word-splitting for user-supplied CXXFLAGS.
# shellcheck disable=SC2086
"$CXX" $CXXFLAGS -o "$OUT/netreceive" "$HERE/netreceive_test.cpp"
"$OUT/netreceive"
"$CXX" $CXXFLAGS -o "$OUT/netjitterpolicy" "$HERE/netjitterpolicy_test.cpp"
"$OUT/netjitterpolicy"
"$CXX" $CXXFLAGS -o "$OUT/polyinwire" "$HERE/polyinwire_test.cpp" "$ROOT/src/polyinwire.cpp"
"$OUT/polyinwire"
"$CXX" $CXXFLAGS -o "$OUT/polysession" "$HERE/polysession_test.cpp" "$ROOT/src/polysession.cpp"
"$OUT/polysession"
"$CXX" $CXXFLAGS -o "$OUT/polyinplayout" "$HERE/polyinplayout_test.cpp" "$ROOT/src/polyinplayout.cpp" "$ROOT/src/polyinwire.cpp"
"$OUT/polyinplayout"
"$CXX" $CXXFLAGS -o "$OUT/polyin_fault" "$HERE/polyin_fault_test.cpp" "$ROOT/src/polyinplayout.cpp" "$ROOT/src/polyinwire.cpp"
"$OUT/polyin_fault"
echo 'All Qt-free receive regression tests passed.'
