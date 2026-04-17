# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-8266 (Wemos D1 Mini) battery-powered environmental sensor station. Uses a two-tier deep sleep architecture: quick 3-second wakes to check a PIR motion sensor, and full 60-second cycles to read sensors and publish via MQTT. When motion is detected, the device stays awake with the OLED display on until 60 seconds of inactivity. Powered by an 18650 Li-Ion battery through an HT7330 3.3V LDO regulator.

**IMPORTANT: Do NOT use fauxmoESP (deprecated). Smart home integration is handled via MQTT.**

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

The device operates in two modes, both ending in deep sleep:

**Idle mode** (no motion):
- Deep sleep 3s → wake → check PIR on D6 → sleep
- Every 20 wakes (~60s): full sensor read + WiFi fast connect + MQTT publish → sleep
- Display off, WiFi off between publishes

**Active mode** (PIR triggered):
- Stays awake continuously — no display flicker
- OLED display on, sensors read every 15s, display updated
- WiFi connects every 30s to publish MQTT, disconnects immediately after
- Checks PIR continuously; resets 60s inactivity timeout on each detection
- When PIR quiet for 60s: publishes motion OFF, clears display, enters idle mode

### Power optimizations
- **WiFi fast connect**: caches router BSSID and channel in RTC memory, skipping channel scan (~1s vs ~3s connect)
- **Reduced TX power**: 10 dBm (default 20.5)
- **3-second idle sleep**: balances PIR responsiveness vs wake overhead
- **WiFi off when not publishing**: only connects for MQTT publish bursts

### Sensor calibration
- **Temperature offset**: 0.9°C subtracted from raw readings (compensates for ESP8266 self-heating)
- **Humidity calibration**: linear correction anchored at 100% — `actual = 100 - (100 - raw) * 1.368`. Calibration point: raw 65.8% = actual 53.2%
- **Humidity temperature compensation**: Magnus formula adjusts RH for the temperature offset

### RTC memory
State persisted across deep sleep cycles via `RtcState` struct:
- Wake counter, cached sensor data, battery voltage
- WiFi BSSID/channel for fast reconnect
- Magic number for validity check (0xE5A70002)

### Modules
- **`sensors.h/cpp`**: DHT22 + BMP180 reading with temperature offset, humidity calibration (linear + Magnus formula). Shared `SensorData` struct.
- **`display.h/cpp`**: 1.3" SH1106 OLED via U8g2. Four-quadrant layout: temp (top-left), humidity (top-right), pressure (bottom-left), battery icon + voltage (bottom-right). Display persists during deep sleep, `clearDisplay()` powers it off.
- **`mqtt.h/cpp`**: MQTT topic defines and shared PubSubClient instance.

### Unused modules (kept from boilerplate, not compiled in)
- `web.h` — web dashboard (not compatible with deep sleep)
- `log.h/cpp` — LittleFS + WebSerial logging (not compatible with deep sleep)
- `memory.h/cpp` — EEPROM helpers (RTC memory used instead)
- `ota.h/cpp` — ArduinoOTA (not compatible with deep sleep)

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

### MQTT Topics
- `environmental_sensor/temperature` — Publish (retained) — DHT22 temperature (°C)
- `environmental_sensor/humidity` — Publish (retained) — DHT22 humidity (%)
- `environmental_sensor/pressure` — Publish (retained) — BMP180 pressure (hPa)
- `environmental_sensor/altitude` — Publish (retained) — BMP180 altitude (m)
- `environmental_sensor/battery` — Publish (retained) — Battery voltage (V)
- `environmental_sensor/motion` — Publish (retained) — PIR state (ON/OFF)
- `environmental_sensor/available` — Publish (retained) — online/offline (LWT)

## Post-Change Checklist

**IMPORTANT: After making any code changes, always update the documentation:**
1. Update this `CLAUDE.md` file to reflect architectural changes, new modules, pin changes, new topics/endpoints
2. Update `README.md` to reflect user-facing changes (new features, setup steps, wiring, MQTT topics, API endpoints, commands)
3. Keep both files in sync with the actual code
