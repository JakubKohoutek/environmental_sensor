# Environmental Sensor

Battery-powered environmental monitoring station built on a Wemos D1 Mini (ESP8266). Measures temperature, humidity, barometric pressure (sea-level adjusted), and detects motion. Uses deep sleep for long battery life, waking briefly to check sensors and publish data via MQTT with Home Assistant auto-discovery. A dimmed 1.3" OLED shows temperature, humidity, battery voltage, and estimated battery days remaining when motion is detected.

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

3. Compile and upload:
   ```bash
   scripts/upload.sh -c
   ```

4. Monitor serial output:
   ```bash
   scripts/monitor.sh
   ```

### Native Tests

Run `bash scripts/test.sh` with a C++11 compiler (`c++`). Tests cover battery-history averaging, estimation and recharge handling, plus the actual sketch's scheduling and display code using hardware stubs. Build for the board with:

```bash
arduino-cli compile --fqbn esp8266:esp8266:d1_mini --libraries ~/Documents/Arduino/libraries .
```

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
- Redraws are skipped when the visible readings and estimated day count are unchanged.
- MQTT stays on the normal 5-minute cadence — PIR events do not trigger extra publishes; the motion flag included in each publish reflects whether the display is currently active.
- After the timeout the OLED is cleared and put into power-save mode, and the device returns to 10-second idle sleeps.
- Scheduling tracks elapsed sleep and awake time rather than counting wakes, so changing sleep intervals does not change the reporting cadence.

### Low Battery Mode (< 3.5V)
- Skips WiFi, sensor reads, and active mode — all non-essential power draws are disabled
- Wakes every 10s while idle to check the PIR
- While motion is present, the full-screen "Low battery!" warning (large crossed-out battery icon + voltage) flashes — shown on one wake, hidden on the next, alternating for as long as the PIR keeps triggering
- Warning flashes use 3-second sleeps and the same reduced OLED contrast.
- When motion stops the display is cleared and the device just cycles PIR checks silently

### Estimated Battery Life

The bottom-left quadrant shows an approximate number of days until the **3.5 V low-battery cutoff**, for example `~28 days`. It is based on voltage history, not measured current or remaining battery capacity.

- Samples battery voltage at roughly 5-minute intervals before sensor/WiFi activity, independently of display activity and skipped MQTT publishes.
- Averages readings into 6-hour samples and retains 29 averages (roughly a week of history) in RTC memory.
- Fits a line to the averaged voltage history and extrapolates to the cutoff. At least five averages spanning 24 hours are required, so initial learning normally takes about **30 hours**.
- Shows `-- days` while learning, during a suspected recharge, or when voltage is flat, rising, or has fallen by less than 0.01 V across the history window. A valid estimate rounding below one day shows `<1 day`; values of 100 days or more use compact text such as `~100d`.
- Resets history after three consecutive samples at least 0.15 V above the lowest stored average, indicating a sustained recharge. A single high reading does not discard the history.
- History survives deep sleep but is lost on power removal. Installing this firmware resets the previous RTC layout and starts learning again.

Li-ion voltage is nonlinear and affected by temperature, load, and recovery after load. The result is a rough indication, not a guaranteed runtime. Sleep-clock drift also affects the estimated elapsed time.

### Features
- **Sea-level pressure**: raw BMP280 reading adjusted for 235m station altitude
- **Battery-life estimate**: rolling voltage-history estimate of days to the low-battery cutoff
- **MQTT auto-discovery**: Home Assistant sensors appear automatically, no manual YAML needed
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

### Home Assistant

Sensors are auto-discovered via MQTT. No manual configuration needed — just ensure the MQTT integration is set up in Home Assistant.

## Display

The 1.3" OLED shows a four-quadrant layout:

```
┌──────────────┬──────────────┐
│  Temp °C     │  Hum %       │
│     22.9     │     53.2     │
├──────────────┼──────────────┤
│  ~28 days    │ [████] 3.87V │
│             │              │
└──────────────┴──────────────┘
```

- **Top left**: Temperature
- **Top right**: Humidity
- **Bottom left**: Estimated battery days remaining (`-- days` while learning)
- **Bottom right**: Battery icon (3.5-4.1V range) with voltage

Sea-level pressure remains available over MQTT.
