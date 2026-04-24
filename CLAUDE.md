# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-8266 (Wemos D1 Mini) battery-powered environmental sensor station. Uses a two-tier deep sleep architecture: quick 3-second wakes to check a PIR motion sensor, and full ~60-second cycles to read sensors and publish via MQTT. When motion is detected, the device stays awake with the OLED display on until 60 seconds of inactivity. Powered by an 18650 Li-Ion battery through an HT7330 3.3V LDO regulator.

**IMPORTANT: Do NOT use fauxmoESP (deprecated). Smart home integration is handled via MQTT with auto-discovery.**

## Build & Upload

This is an Arduino IDE project targeting ESP8266. The sketch is `environmental_sensor.ino`.

- **Board**: Wemos D1 Mini — FQBN: `esp8266:esp8266:d1_mini`
- **Compile**: `arduino-cli compile --fqbn esp8266:esp8266:d1_mini --libraries ~/Documents/Arduino/libraries .`
- **Upload + monitor**: `scripts/upload.sh -c`
- **Monitor only**: `scripts/monitor.sh`

### Required Libraries

- `ESP8266WiFi`, `PubSubClient`
- `DHT sensor library` + `Adafruit Unified Sensor` — DHT22 temperature/humidity sensor
- `Adafruit BMP085 Library` — BMP180 barometric pressure sensor
- `U8g2` — 1.3" SH1106 128x64 I2C OLED display
- WiFi and MQTT credentials come from `<credentials.h>` (must define `STASSID`, `STAPSK`, `MQTT_SERVER`, `MQTT_PORT`, and optionally `MQTT_USER`/`MQTT_PASS`) — this file is not in the repo

## Architecture

### Two-tier deep sleep (`environmental_sensor.ino`)

**Idle mode** (no motion):
- Deep sleep 3s → wake → check PIR on D6 → sleep
- Every 40 wakes (~120s): full sensor read + WiFi fast connect + MQTT publish → sleep
- Adaptive publish: skips MQTT if values haven't changed significantly (thresholds: ±0.2°C temp, ±1% hum, ±0.1hPa pressure, ±0.05V battery). Forces publish after 5 skipped cycles (≈10 min max silence). Pressure threshold matches the published precision so even small but meaningful changes are reported.
- Display off, WiFi off between publishes

**Display-active mode** (PIR triggered):
- MCU does NOT stay awake — the device continues its 3s deep-sleep cycles
- On PIR HIGH, the OLED is painted with current readings and retains that frame across sleeps (OLEDs hold their state without extra current)
- On every wake while the display is lit, PIR is re-polled; each PIR HIGH refills a 20-wake (~60s) countdown
- Sensors + display redraw on a `DISPLAY_REFRESH_INTERVAL` schedule (every 5 wakes = ~15s), so values stay current without drawing every 3s
- MQTT publishing stays on the normal 2-min idle cadence — PIR transitions do not trigger extra publishes. The published motion flag reflects current display-active state.
- When the countdown reaches 0 (20 consecutive wakes with no PIR HIGH), the OLED is cleared via `clearDisplay()` and the device falls back to pure idle

**Low battery mode** (< 3.5V):
- Skips WiFi, sensor reads, and active mode — the device still wakes every 3s (same interval as idle mode) to check the PIR so motion is detected promptly
- While PIR is HIGH, the full-screen "Low battery!" warning (large crossed-out battery icon + voltage) flashes by toggling on every wake: shown one cycle (~3s), hidden the next, repeating for as long as motion is held
- `rtcState.lowBatteryWarningShown` holds the flash state across deep-sleep wakes; when PIR drops LOW the display is cleared and the flag reset so the next motion event starts the cycle from "shown"
- Battery ADC is averaged over 2 samples to smooth noise near the 3.5V threshold without adding material wake time

### Safety
- **Watchdog timer**: 8-second hardware WDT enabled. Fed explicitly inside the WiFi connect loop.

### Power optimizations
- **WiFi fast connect**: caches router BSSID and channel in RTC memory (~1s vs ~3s connect)
- **Reduced TX power**: 10 dBm (default 20.5)
- **3-second idle sleep**: balances PIR responsiveness vs wake overhead
- **WiFi off when not publishing**: only connects for MQTT publish bursts
- **Adaptive publish**: skips WiFi entirely when sensor values unchanged
- **Full cycle cadence**: full sensor/MQTT cycle every ~120s in idle mode (vs every 3s PIR poll)

### Sensor calibration
- **Temperature offset**: 0.3°C subtracted from raw readings (ESP8266 self-heating)
- **Humidity calibration**: linear correction anchored at 100% — `actual = 100 - (100 - raw) * 1.225`
- **Sea-level pressure**: station pressure adjusted for 235m altitude using barometric formula
- **Zambretti forecast**: weather prediction based on sea-level pressure and trend using short Czech OLED labels (e.g., "Jasno", "Brzy dest", "Bourky")

### Trend tracking
- Circular buffer of last 5 readings stored in RTC memory
- Compares newest to oldest reading to determine trend direction
- Thresholds: ±0.3°C temp, ±1.5% humidity, ±0.5hPa pressure
- Displayed as small triangle arrows (↑↓→) next to values on OLED

### MQTT Discovery
- Publishes Home Assistant auto-discovery messages on first MQTT connect
- Discovery config sent to `homeassistant/sensor/environmental_sensor/*/config`
- Persisted in RTC memory so discovery only runs once per power cycle

### RTC memory
State persisted across deep sleep cycles via `RtcState` struct:
- Wake counter, cached sensor data, battery voltage
- WiFi BSSID/channel for fast reconnect
- Last published values for adaptive publish
- Trend history circular buffer (5 entries)
- Discovery published flag
- Low-battery warning flash state (toggled each wake while PIR HIGH)
- Magic number for validity check (0xE5A70004)

### Modules
- **`sensors.h/cpp`**: DHT22 + BMP180 reading with temperature offset, humidity calibration (linear + Magnus), sea-level pressure calculation (235m altitude), Zambretti weather forecast. Shared `SensorData` struct. DHT22 is read once per cycle — no in-call retries. The sensor is often unresponsive for 10–30s after an ESP8266 deep-sleep wake, likely a signal-integrity issue; recommended hardware fix is a **10kΩ external pull-up between D5 (DHT22 data) and 3V3**.
- **`display.h/cpp`**: 1.3" SH1106 OLED via U8g2. Four-quadrant layout: temp (top-left), humidity (top-right), Zambretti forecast (bottom-left), battery icon + voltage (bottom-right). Dedicated full-screen low-battery warning view (crossed-out battery icon + voltage).
- **`mqtt.h/cpp`**: MQTT topic defines and shared PubSubClient instance.

### Hardware

#### Pin assignments

| Pin | GPIO | Function         | Direction | Component         |
|-----|------|------------------|-----------|-------------------|
| D0  | 16   | Deep sleep wake  | OUTPUT    | Wired to RST      |
| D1  | 5    | I2C SCL          | I/O       | BMP180 + OLED     |
| D2  | 4    | I2C SDA          | I/O       | BMP180 + OLED     |
| D4  | 2    | Built-in LED     | OUTPUT    | LED (active LOW)  |
| D5  | 14   | DHT22 data       | INPUT     | DHT22             |
| D6  | 12   | PIR sensor       | INPUT     | PIR motion sensor |
| A0  |      | Battery voltage  | INPUT     | 100kΩ divider     |

#### Power
- 18650 Li-Ion battery → HT7330 LDO (3.3V output) → Wemos 3V3 pin
- Battery voltage monitoring: 100kΩ resistor from battery+ to A0
- ADC calibration: `VBAT_MULTIPLIER = 0.004007` (4.1V at ADC max)
- Battery icon range: 3.5V (empty, LDO dropout) to 4.1V (full)
- Low battery threshold: 3.5V (skips WiFi/sensors; keeps the normal 3s PIR-poll cycle for the warning flash)

### MQTT Topics
- `environmental_sensor/temperature` — Publish (retained) — DHT22 temperature (°C)
- `environmental_sensor/humidity` — Publish (retained) — DHT22 humidity (%)
- `environmental_sensor/pressure` — Publish (retained) — Sea-level adjusted pressure (hPa)
- `environmental_sensor/altitude` — Publish (retained) — BMP180 altitude (m)
- `environmental_sensor/battery` — Publish (retained) — Battery voltage (V)
- `environmental_sensor/motion` — Publish (retained) — PIR state (ON/OFF)
- `environmental_sensor/available` — Publish (retained) — online/offline (LWT)

## Post-Change Checklist

**IMPORTANT: After making any code changes, always update the documentation:**
1. Update this `CLAUDE.md` file to reflect architectural changes, new modules, pin changes, new topics/endpoints
2. Update `README.md` to reflect user-facing changes (new features, setup steps, wiring, MQTT topics, API endpoints, commands)
3. Keep both files in sync with the actual code
