# Environmental Sensor

Battery-powered environmental monitoring station built on a Wemos D1 Mini (ESP8266). Measures temperature, humidity, barometric pressure (sea-level adjusted), and detects motion. Uses deep sleep for long battery life, waking briefly to check sensors and publish data via MQTT with Home Assistant auto-discovery. A dimmed 1.3" OLED shows temperature, humidity, battery voltage, and battery percentage when motion is detected.

## Hardware

- **Wemos D1 Mini** (ESP8266)
- **AHT20 + BMP280 combo module** — temperature, humidity, and barometric pressure over a single I2C bus
- **1.3" 128x64 OLED** — SH1106 I2C display
- **PIR motion sensor** — triggers display and active mode
- **18650 Li-Ion battery** — via HT7330 3.3V LDO regulator

### Wiring

| Component          | Pin  | Wemos D1 Mini | GPIO | Notes                        |
|--------------------|------|---------------|------|------------------------------|
| Deep sleep         | D0   | RST           | 16   | Required for wake from sleep |
| AHT20 + BMP280     | SCL  | D1            | 5    | I2C clock (shared bus)       |
| AHT20 + BMP280     | SDA  | D2            | 4    | I2C data (shared bus)        |
| OLED               | SCL  | D1            | 5    | I2C clock (shared bus)       |
| OLED               | SDA  | D2            | 4    | I2C data (shared bus)        |
| PIR sensor         | OUT  | D6            | 12   |                              |
| Battery (via 100kΩ)| +    | A0            |      | Voltage monitoring           |

- All I2C peripherals share the bus on D1/D2. I2C addresses: AHT20 = 0x38, BMP280 = 0x76 (falls back to 0x77), OLED = 0x3C.
- Power all sensors from 3.3V (HT7330 LDO output)
- D0 must be wired to RST for deep sleep wake
- 100kΩ resistor from battery+ to A0 for voltage monitoring
- If the combo module has a power LED (common on GY-style boards), remove it — it drains 2–5 mA continuously and will dominate battery consumption

### Power Supply

```
18650 Battery (+) ──→ HT7330 VIN ──→ HT7330 VOUT (3.3V) ──→ Wemos 3V3 pin
18650 Battery (+) ──[100kΩ]──→ Wemos A0 (battery monitoring)
18650 Battery (-) ──→ GND
```

The HT7330 LDO regulates the battery (3.5-4.2V) to a stable 3.3V. Below 3.5V the device enters low battery mode — skipping WiFi and sensor reads, polling the PIR every 10 seconds while idle, and flashing the on-screen warning every 3 seconds while motion is present.

## Setup

1. Install required libraries via Arduino Library Manager:
   - `Adafruit AHTX0`
   - `Adafruit Unified Sensor`
   - `Adafruit BMP280 Library`
   - `U8g2`
   - `PubSubClient`

2. Create a `credentials` library at `~/Documents/Arduino/libraries/credentials/credentials.h`:
   ```cpp
   #define STASSID "YourWiFiSSID"
   #define STAPSK  "YourWiFiPassword"
   #define MQTT_SERVER "homeassistant.local"
   #define MQTT_PORT   1883
   ```
   Optionally define `OTA_PASSWORD` in the same private header to require a password for wireless uploads. Without it, ArduinoOTA does not require an upload password.

3. Compile and upload:
   ```bash
   scripts/upload.sh -c
   ```

4. Monitor serial output:
   ```bash
   scripts/monitor.sh
   ```

### Native Tests

Run `bash scripts/test.sh` with a C++11 compiler (`c++`). Tests cover battery percentage, icon fill, layout, redraws, scheduling, and MQTT-triggered OTA (command clearing, timeout, low battery, errors, WiFi loss, and upload progress) using hardware stubs. Build for the board with:

```bash
arduino-cli compile --fqbn esp8266:esp8266:d1_mini --libraries ~/Documents/Arduino/libraries .
```

### Wireless Updates (MQTT-triggered OTA)

Install this firmware once over USB using `scripts/upload.sh -c`. Later updates can use WiFi without a hardware button:

1. Compile the new firmware using the command above before requesting the update window.
2. In Home Assistant, open **Air Quality → Bathroom → Environmental Sensor OTA** and press **Prepare OTA update**.
3. Wait for **Last OTA status** to become `waiting`. The request is retained by MQTT while the sensor sleeps. With adaptive publishing, pickup can take about **30 minutes** under normal connectivity; failures can delay it further.
4. The OLED shows **OTA update**, a **five-minute countdown**, and the device's IP address. During this window, upload from a computer on the same network:
   ```bash
   bash scripts/upload-ota.sh 192.168.68.50
   ```
   Use the IP shown on the OLED (the address above is an example); `environmental-sensor.local` also works if your computer resolves mDNS hostnames. The script uploads the last compiled binary using the installed ESP8266 `espota.py` with system `python3`, avoiding the bundled Python/network-discovery problems encountered with Arduino CLI uploads on this Mac. If `OTA_PASSWORD` is configured in firmware, set the same password in the shell's `OTA_PASSWORD` environment variable before running the script.
5. The OLED shows upload progress and then **OTA complete** before rebooting into the new firmware. Keep OTA support in subsequent firmware to preserve wireless updates.

The Home Assistant button opens the update window; it does not transfer a firmware file. Its entity is `button.environmental_sensor_prepare_ota`; the status entity is `sensor.environmental_sensor_ota_status`. These are auto-discovered and belong to the existing Environmental Sensor device. The button intentionally remains usable while the sensor sleeps.

OTA requires a measured battery voltage of **at least 3.60 V**, even when USB is connected. A lower reading rejects and clears the request with `low_battery`; charge or replace the battery, then press the button again. Below the normal 3.5 V operating cutoff, WiFi is disabled entirely, so queued requests cannot be received.

The firmware checks commands on existing MQTT connections, adding up to one second of listening without introducing extra WiFi wakeups. It clears an accepted retained request and waits for the broker's echo before entering OTA mode, preventing the same request from reopening the window after a reboot. If no upload starts within five minutes, the display turns off and normal sleep scheduling resumes. A started transfer may finish beyond that waiting deadline. Upload errors or WiFi loss also return to normal operation.

The retained status reports `idle`, `waiting`, `updating`, `complete`, `timed_out`, `error`, `wifi_lost`, `low_battery`, or `request_failed`. It is the **last reported status**, not continuous availability: an abrupt power/network loss can leave it stale. Each subsequent MQTT connection reports `idle` before checking for another request. A request can be cancelled before pickup by publishing an empty retained payload to `environmental_sensor/ota/set`.

Serial logging is off by default. For diagnosis, compile with `--build-property compiler.cpp.extra_flags=-DDEBUG=1` and monitor at 74880 baud; compile normally again to restore quiet operation.

## How It Works

### Idle Mode (no motion)
- Deep sleeps for 10 seconds, wakes, checks the PIR sensor. Display activation can take up to roughly 10 seconds; PIR pulses shorter than the polling interval can be missed.
- Every ~5 minutes: reads all sensors and, if needed, connects WiFi, publishes MQTT, and disconnects. The first boot also runs a full cycle.
- **Adaptive publishing**: skips MQTT if changes stay below 0.2°C temperature, 1% humidity, 0.5 hPa pressure, and 0.05 V battery. Pressure is still published with 0.1 hPa precision. After 5 skipped cycles the next cycle forces a publish (~30 minutes between reports when unchanged, assuming successful connections).
- Display is off, WiFi is off between publishes
- RF behavior is unchanged: deep sleep still uses `WAKE_NO_RFCAL`, with the existing WiFi connection and transmit-power settings.

### Display-Active Mode (motion detected)
- MCU switches to 3-second sleep cycles — the OLED retains its frame across deep sleeps, but **continues consuming power while lit**.
- PIR HIGH on any wake renews a roughly 30-second display timeout. Continuous motion can still keep the display on.
- OLED contrast is set to **64/255** on each initialization, including the low-battery warning.
- Sensors + display redraw every ~15 seconds while the display is lit
- Redraws are skipped when the visible readings and battery percentage are unchanged.
- MQTT stays on the normal 5-minute cadence — PIR events do not trigger extra publishes; the motion flag included in each publish reflects whether the display is currently active.
- After the timeout the OLED is cleared and put into power-save mode, and the device returns to 10-second idle sleeps.
- Scheduling tracks elapsed sleep and awake time rather than counting wakes, so changing sleep intervals does not change the reporting cadence.

### Low Battery Mode (< 3.5V)
- Skips WiFi, sensor reads, and active mode — all non-essential power draws are disabled
- Wakes every 10s while idle to check the PIR
- While motion is present, the full-screen "Low battery!" warning (large crossed-out battery icon + voltage) flashes — shown on one wake, hidden on the next, alternating for as long as the PIR keeps triggering
- Warning flashes use 3-second sleeps and the same reduced OLED contrast.
- When motion stops the display is cleared and the device just cycles PIR checks silently

### Battery Percentage

The battery icon has a percentage beside it, calculated linearly from the current battery voltage:

- **3.60 V or below: 0%**, with an empty icon.
- **4.05 V or above: 100%**, with a full icon.
- Between those limits: `(voltage - 3.60) / (4.05 - 3.60) * 100`, rounded to the nearest whole percent. For example, 3.87 V shows 60%.
- The icon uses the same percentage, scaled to its 12-pixel interior width.

This is a voltage-based indicator, not a measurement of true remaining capacity. It replaces the days-to-depletion estimate and needs no voltage history or learning period. Voltage is still averaged over two ADC samples on each wake. Installing this firmware resets the previous RTC layout.

The **3.5 V low-battery operating cutoff is unchanged**: at 3.59 V, the normal screen shows 0%, but low-power warning mode starts only below 3.5 V. MQTT continues to report battery voltage in volts, so existing Home Assistant automations are unchanged.

### Features
- **Sea-level pressure**: raw BMP280 reading adjusted for 235m station altitude
- **Battery percentage**: voltage-based 0-100% indicator with matching battery icon fill
- **MQTT auto-discovery**: Home Assistant sensors appear automatically, no manual YAML needed
- **MQTT-triggered OTA**: retained update request, five-minute awake window, OLED countdown/IP/progress, and Home Assistant controls
- **Watchdog timer**: 8-second hardware WDT prevents hangs
- **WiFi fast connect**: caches router BSSID/channel for ~1s connection time
- **Low-power sensing**: AHT20 draws ~1 µA idle, BMP280 runs in FORCED mode (~0.1 µA between reads)
- **Sensor calibration**: temperature offset (0.8°C subtracted — AHT20 reads ~0.8°C high) and humidity linear correction (default 1.0× pass-through — AHT20 is factory-calibrated)

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `environmental_sensor/temperature` | Publish | Temperature in °C |
| `environmental_sensor/humidity` | Publish | Relative humidity in % |
| `environmental_sensor/pressure` | Publish | Sea-level adjusted pressure in hPa |
| `environmental_sensor/altitude` | Publish | Altitude in m |
| `environmental_sensor/battery` | Publish | Battery voltage in V |
| `environmental_sensor/motion` | Publish | PIR state: ON / OFF |
| `environmental_sensor/available` | Publish | Online status (LWT) |
| `environmental_sensor/ota/set` | Subscribe / clear | Retained `START` requests an OTA window; device clears it before starting |
| `environmental_sensor/ota/status` | Publish | Retained last OTA status |

### Home Assistant

Sensors are auto-discovered via MQTT. No manual configuration needed — just ensure the MQTT integration is set up in Home Assistant.

The **Air Quality → Bathroom** dashboard includes an **Environmental Sensor OTA** card with the update-request button and last status. On a new Home Assistant installation, add `button.environmental_sensor_prepare_ota` and `sensor.environmental_sensor_ota_status` to the desired dashboard after discovery.

## Display

The 1.3" OLED shows sensor readings above a horizontal divider and battery information below it, with no vertical divider:

```
┌─────────────────────────────┐
│  Temp °C        Hum %       │
│     22.9          53.2      │
├─────────────────────────────┤
│   3.87V         [███  ] 60% │
│                             │
└─────────────────────────────┘
```

- **Top left**: Temperature
- **Top right**: Humidity
- **Bottom left**: Battery voltage
- **Bottom right**: Battery icon with percentage (3.60 V = 0%, 4.05 V = 100%)

Sea-level pressure remains available over MQTT.
