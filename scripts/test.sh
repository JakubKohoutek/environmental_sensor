#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

test_binary="$(mktemp "${TMPDIR:-/tmp}/environmental-sensor-test.XXXXXX")"
trap 'rm -f "$test_binary"' EXIT

c++ -std=c++11 -Wall -Wextra -Werror -pedantic -Itests/stubs \
    tests/firmware_test.cpp display.cpp mqtt.cpp ota_update.cpp -o "$test_binary"
"$test_binary"
