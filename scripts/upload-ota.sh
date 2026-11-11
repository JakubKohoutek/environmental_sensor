#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  echo "Usage: bash scripts/upload-ota.sh <device-ip-or-hostname>"
  echo "Compile first, then request OTA from Home Assistant and wait for the OLED countdown."
  echo "Set OTA_PASSWORD in the environment if the firmware requires an upload password."
  exit 0
fi

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
  echo "Usage: bash scripts/upload-ota.sh <device-ip-or-hostname>" >&2
  exit 2
fi

properties="$(arduino-cli compile --fqbn esp8266:esp8266:d1_mini --show-properties .)"
platform_path="$(printf '%s\n' "$properties" | sed -n 's/^runtime.platform.path=//p')"
build_path="$(printf '%s\n' "$properties" | sed -n 's/^build.path=//p')"
binary="${build_path}/environmental_sensor.ino.bin"

if [ -z "$platform_path" ] || [ -z "$build_path" ] || [ ! -f "$binary" ]; then
  echo "Compile the firmware first:" >&2
  echo "arduino-cli compile --fqbn esp8266:esp8266:d1_mini --libraries ~/Documents/Arduino/libraries ." >&2
  exit 1
fi

# Use system Python and a direct address, avoiding bundled-Python and mDNS discovery issues.
exec python3 "$platform_path/tools/espota.py" \
  --ip "$1" --port 8266 --auth "${OTA_PASSWORD:-}" --file "$binary"
