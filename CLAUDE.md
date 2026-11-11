# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-8266 (Wemos D1 Mini) battery-powered environmental sensor station. Uses 10-second idle deep sleeps to poll a PIR and roughly 5-minute full sensor/MQTT cycles. Motion activates a dimmed OLED with temperature, humidity, battery voltage, and battery percentage. While the display is on, the MCU still sleeps for 3 seconds between polls; it turns the display off roughly 30 seconds after the last PIR HIGH. Powered by an 18650 Li-Ion battery through an HT7330 3.3V LDO regulator.

**IMPORTANT: Do NOT use fauxmoESP (deprecated). Smart home integration is handled via MQTT with auto-discovery.**

## Build & Upload

This is an Arduino IDE project targeting ESP8266. The sketch is `environmental_sensor.ino`.

- **Board**: Wemos D1 Mini — FQBN: `esp8266:esp8266:d1_mini`
- **Compile**: `arduino-cli compile --fqbn esp8266:esp8266:d1_mini --libraries ~/Documents/Arduino/libraries .`
- **Upload + monitor**: `scripts/upload.sh -c`
- **OTA upload**: compile first, request OTA from Home Assistant, then `bash scripts/upload-ota.sh <OLED-IP>`. Uses the installed ESP8266 `espota.py` with system `python3` rather than the bundled interpreter and CLI network-port discovery. Optional upload password comes from the `OTA_PASSWORD` environment variable.
- **Monitor only**: `scripts/monitor.sh`
- **Native tests**: `bash scripts/test.sh` (C++11 compiler; percentage, display, scheduling, and OTA request/timeout/error/progress behavior with hardware stubs)

### Required Libraries

- `ESP8266WiFi`, `ArduinoOTA` (bundled with ESP8266 core), `PubSubClient`
- `Adafruit AHTX0` + `Adafruit Unified Sensor` — AHT20 temperature/humidity sensor (I2C)
- `Adafruit BMP280 Library` — BMP280 barometric pressure sensor (I2C)
- `U8g2` — 1.3" SH1106 128x64 I2C OLED display
- WiFi and MQTT credentials come from `<credentials.h>` (must define `STASSID`, `STAPSK`, `MQTT_SERVER`, `MQTT_PORT`, and optionally `MQTT_USER`/`MQTT_PASS` and `OTA_PASSWORD`) — this file is not in the repo. Without `OTA_PASSWORD`, OTA uploads do not require a password.

## Architecture

### Two-tier deep sleep (`environmental_sensor.ino`)

**Idle mode** (no motion):
- Deep sleep 10s → wake → check PIR on D6 → sleep. Activation latency is up to roughly 10s; shorter PIR pulses may be missed.
- Every ~300s: full sensor read + optional WiFi fast connect + MQTT publish → sleep. First boot forces a full cycle.
- Adaptive publish: skips MQTT if values haven't changed significantly (thresholds: ±0.2°C temp, ±1% hum, ±0.5hPa pressure, ±0.05V battery). After 5 skipped cycles, the next cycle forces a publish (~30 min between unchanged reports when connections succeed). Pressure retains 0.1 hPa published precision.
- Display off, WiFi off between publishes
- Elapsed seconds and subsecond awake-time remainder persist in RTC memory. Scheduling uses unsigned elapsed-time differences, not wake counts, so idle/active transitions preserve timing.

**Display-active mode** (PIR triggered):
- MCU does NOT stay awake — the device continues its 3s deep-sleep cycles
- On PIR HIGH, the OLED is painted and retains that frame across sleeps; the lit panel still consumes power.
- Each PIR HIGH renews `DISPLAY_ON_SECONDS` (30s). Continuous motion can keep the panel lit.
- `initiateDisplay()` sets contrast to 64/255, including warning screens.
- Sensors refresh on the `DISPLAY_REFRESH_SECONDS` schedule (~15s); display redraws are skipped unless visible readings or the battery percentage change.
- MQTT publishing stays on the normal 5-min cadence — PIR transitions do not trigger extra publishes. The published motion flag reflects current display-active state.
- At timeout, `clearDisplay()` clears and power-saves the OLED, and polling returns to 10-second idle sleeps.

**Low battery mode** (< 3.5V):
- Skips WiFi, sensor reads, and active mode — the device wakes every 10s while idle to check the PIR
- While PIR is HIGH, the full-screen "Low battery!" warning (large crossed-out battery icon + voltage) flashes by toggling on every wake: shown one cycle (~3s), hidden the next, repeating for as long as motion is held
- `rtcState.lowBatteryWarningShown` holds the flash state across deep-sleep wakes; when PIR drops LOW the display is cleared and the flag reset so the next motion event starts the cycle from "shown"
- Warning flashes use 3-second sleeps; the display-active flag is cleared on entry, and any remaining warning is cleared on battery recovery.
- Battery ADC is averaged over 2 samples to smooth noise near the 3.5V threshold without adding material wake time

### Safety
- **Watchdog timer**: 8-second hardware WDT enabled. Fed explicitly inside the WiFi connect loop.

### MQTT-triggered OTA (`ota_update.h/cpp`)
- `publishSensorData()` calls `checkOtaRequest()` before disconnecting MQTT. No additional WiFi connections are introduced; adaptive publishing can delay request pickup by roughly 30 minutes under normal connectivity.
- Each MQTT session publishes `idle`, subscribes to `environmental_sensor/ota/set` at QoS 1, and listens for up to one second. Only the exact payload `START` triggers OTA.
- Requests are retained by the broker while the device sleeps. The device publishes an empty retained payload and waits up to one second for its subscribed echo before starting OTA. If clearing fails, it reports `request_failed` and does not enter OTA.
- Measured voltage must be >=3.60 V, including when USB is connected. Below this, a received request is cleared and rejected with `low_battery`. Below 3.5 V, normal low-battery mode prevents MQTT connections entirely.
- ArduinoOTA uses hostname `environmental-sensor` and its default port 8266. Optional `OTA_PASSWORD` is read from the private credentials header.
- The MCU stays awake for `OTA_WAIT_MS` (five minutes) with the OLED showing `OTA update`, remaining seconds, and IP. Upload callbacks display progress, completion, or failure and publish status. Watchdog and MQTT are serviced during waiting/progress.
- ESP8266 core 3.0.2 closes all TCP clients before `onStart`; that callback reconnects MQTT using the shared `connectMqtt()` helper and restores availability before publishing `updating`. A broker reconnect failure does not block the firmware transfer. `onEnd` publishes `complete` and disconnects MQTT gracefully before reboot.
- ArduinoOTA handles transfers synchronously: once started, an upload is allowed to finish beyond the waiting deadline. Success reboots; timeout/error/WiFi loss clears the OLED and resumes the caller's normal WiFi disconnect/deep sleep path. The existing ESP8266 core 3.0.2 lacks `ArduinoOTA.end()`, so cleanup relies on that disconnect/sleep.
- After a window, `displayOn` is cleared so a subsequent PIR event redraws correctly. Awake time is accounted for by `saveAndSleep()`; no OTA state is persisted in RTC.
- `environmental_sensor/ota/status` retains the last reported status, not live availability. It may be stale after abrupt power/network loss. Each later MQTT session resets it to `idle`.
- First installation requires USB. MQTT discovery exposes `button.environmental_sensor_prepare_ota` and `sensor.environmental_sensor_ota_status`. The Air Quality dashboard's Bathroom tab has an Environmental Sensor OTA card; the button queues a request, not the firmware binary.

### Power optimizations
- **WiFi fast connect**: caches router BSSID and channel in RTC memory (~1s vs ~3s connect)
- **Reduced TX power**: 10 dBm (default 20.5)
- **10-second idle sleep**: reduces boot overhead; 3-second sleeps while display-active
- **Dimmed, shorter display use**: contrast 64/255 and 30-second inactivity timeout
- **WiFi off when not publishing**: only connects for MQTT publish bursts
- **Adaptive publish**: skips WiFi entirely when sensor values unchanged
- **Full cycle cadence**: full sensor/MQTT opportunity every ~300s in both modes
- **RF unchanged**: retain `WAKE_NO_RFCAL` and the existing WiFi connection/disconnection behavior; do not substitute RF-disabled wakes.

### Sensor calibration
- **Temperature offset**: 0.8°C subtracted from raw AHT20 reading (AHT20 reads ~0.8°C high vs a reference) — retune if needed
- **Humidity calibration**: linear correction anchored at 100% — `actual = 100 - (100 - raw) * HUMIDITY_CAL_FACTOR`. Factor defaults to 1.0 (pass-through) since AHT20 is factory-calibrated to ±2% RH.
- **Sea-level pressure**: station pressure adjusted for 235m altitude using barometric formula
- **BMP280 mode**: FORCED (one-shot measurement per read, sensor sleeps between reads — ~0.1 µA idle)

### Battery percentage
- `batteryPercentage()` in `display.cpp` maps the current voltage linearly from 3.60 V (0%) to 4.05 V (100%), rounds to the nearest whole percent, and clamps to 0-100%.
- Hardware measures voltage, not current; this is a voltage-based indicator, not true remaining capacity. There is no days-to-depletion estimate, history, or learning period.
- The percentage appears beside the battery icon. The 12-pixel interior fill is `percentage * 12 / 100`, so the text and icon use the same scale.
- Redraw detection compares percentages calculated from the current and last-rendered voltage, including changes smaller than the voltage redraw threshold.
- Two-sample ADC averaging remains unchanged. The operating cutoff is still 3.5 V, independent of the 3.60 V empty indicator; MQTT still publishes voltage in volts.

### MQTT Discovery
- Publishes Home Assistant auto-discovery messages on first MQTT connect
- Discovery config sent to `homeassistant/sensor/environmental_sensor/*/config`, with motion under `homeassistant/binary_sensor/environmental_sensor/motion/config`
- First MQTT connection and every 30 successful connections thereafter; counters persist in RTC memory
- OTA button discovery: `homeassistant/button/environmental_sensor/ota/config`; OTA status discovery: `homeassistant/sensor/environmental_sensor/ota_status/config`. The queued-request button has no availability gate so it works while the device sleeps.
- `connectMqtt()` in `mqtt.cpp` expands the PubSubClient packet buffer to 768 bytes for discovery payloads and limits MQTT socket response waits to two seconds.

### RTC memory
State persisted across deep sleep cycles via `RtcState` struct:
- Elapsed-time scheduler, subsecond awake remainder, cached sensor data, battery voltage
- WiFi BSSID/channel for fast reconnect
- Last published values for adaptive publish
- Discovery published flag
- Low-battery warning flash state (toggled each wake while PIR HIGH)
- Display state, last-motion/refresh timestamps and rendered-value cache
- Magic number for validity check (0xE5A7000E); upgrading resets previous state and forces the new OTA discovery entries to be published
- Compile-time assertions enforce word alignment and the 512-byte RTC user-memory limit (current layout: 128 bytes)

### Modules
- **`sensors.h/cpp`**: AHT20 (I2C 0x38) + BMP280 (I2C 0x76, falls back to 0x77) reading with temperature offset, humidity calibration (linear + Magnus), sea-level pressure calculation (235m altitude). Shared `SensorData` struct. BMP280 runs in FORCED mode so it sleeps between reads.
- **`display.h/cpp`**: dimmed 1.3" SH1106 OLED via U8g2. Temperature (top-left), humidity (top-right), voltage (bottom-left), battery icon + percentage (bottom-right). Horizontal divider only, no vertical line. Shared voltage-to-percentage helper for rendering and redraw detection. Dedicated full-screen low-battery warning and OTA waiting/progress/result views.
- **`ota_update.h/cpp`**: MQTT OTA discovery, retained-request handshake, battery guard, and bounded ArduinoOTA maintenance window.
- **`mqtt.h/cpp`**: MQTT topic defines, shared PubSubClient instance, and reusable authenticated connection helper.
- **`debug.h`**: `DBG_*` logging macros; `DEBUG` defaults to 0 and can be overridden with `--build-property compiler.cpp.extra_flags=-DDEBUG=1` for serial diagnostics at 74880 baud.

### Hardware

#### Pin assignments

| Pin | GPIO | Function         | Direction | Component              |
|-----|------|------------------|-----------|------------------------|
| D0  | 16   | Deep sleep wake  | OUTPUT    | Wired to RST           |
| D1  | 5    | I2C SCL          | I/O       | AHT20 + BMP280 + OLED  |
| D2  | 4    | I2C SDA          | I/O       | AHT20 + BMP280 + OLED  |
| D4  | 2    | Built-in LED     | OUTPUT    | LED (active LOW)       |
| D6  | 12   | PIR sensor       | INPUT     | PIR motion sensor      |
| A0  |      | Battery voltage  | INPUT     | 100kΩ divider          |

#### Power
- 18650 Li-Ion battery → HT7330 LDO (3.3V output) → Wemos 3V3 pin
- Battery voltage monitoring: 100kΩ resistor from battery+ to A0
- ADC calibration: `VBAT_MULTIPLIER = 0.004007` (4.1V at ADC max)
- Battery icon and percentage range: 3.60V (empty, 0%) to 4.05V (full, 100%)
- Low battery threshold: 3.5V (skips WiFi/sensors; 10s idle PIR polls and 3s warning flashes while PIR is HIGH)

### MQTT Topics
- `environmental_sensor/temperature` — Publish (retained) — AHT20 temperature (°C)
- `environmental_sensor/humidity` — Publish (retained) — AHT20 humidity (%)
- `environmental_sensor/pressure` — Publish (retained) — Sea-level adjusted pressure (hPa)
- `environmental_sensor/altitude` — Publish (retained) — BMP280 altitude (m)
- `environmental_sensor/battery` — Publish (retained) — Battery voltage (V)
- `environmental_sensor/motion` — Publish (retained) — PIR state (ON/OFF)
- `environmental_sensor/available` — Publish (retained) — online/offline (LWT)
- `environmental_sensor/ota/set` — Subscribe at QoS 1 / publish empty retained payload to clear — exact retained `START` requests an OTA window
- `environmental_sensor/ota/status` — Publish (retained) — last status: idle/waiting/updating/complete/timed_out/error/wifi_lost/low_battery/request_failed

## Post-Change Checklist

**IMPORTANT: After making any code changes, always update the documentation:**
1. Update this `CLAUDE.md` file to reflect architectural changes, new modules, pin changes, new topics/endpoints
2. Update `README.md` to reflect user-facing changes (new features, setup steps, wiring, MQTT topics, API endpoints, commands)
3. Keep both files in sync with the actual code
