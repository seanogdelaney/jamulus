#!/bin/sh
set -eu
cd "$(dirname "$0")"
${CXX:-c++} -std=c++17 -Wall -Wextra -pedantic -o /tmp/jamulus-netreceive-test netreceive_test.cpp
/tmp/jamulus-netreceive-test
