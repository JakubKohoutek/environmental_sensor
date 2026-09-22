#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

test_binary="$(mktemp "${TMPDIR:-/tmp}/environmental-sensor-test.XXXXXX")"
trap 'rm -f "$test_binary"' EXIT

c++ -std=c++11 -Wall -Wextra -Werror -pedantic \
    tests/battery_life_test.cpp battery_life.cpp -o "$test_binary"
"$test_binary"

c++ -std=c++11 -Wall -Wextra -Werror -pedantic -Itests/stubs \
    tests/firmware_test.cpp battery_life.cpp display.cpp mqtt.cpp -o "$test_binary"
"$test_binary"
