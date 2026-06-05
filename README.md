# Environmental Sensor

Battery-powered environmental monitoring station built on a Wemos D1 Mini (ESP8266). Measures temperature, humidity, barometric pressure (sea-level adjusted), and detects motion. Uses deep sleep for long battery life, waking briefly to check sensors and publish data via MQTT with Home Assistant auto-discovery. A 1.3" OLED display with trend arrows shows live readings when motion is detected.

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

The HT7330 LDO regulates the battery (3.5-4.2V) to a stable 3.3V. Below 3.5V the device enters low battery mode — skipping WiFi and sensor reads, but keeping the same 3-second PIR-poll cycle so the on-screen low-battery warning can flash.

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

## How It Works

### Idle Mode (no motion)
- Deep sleeps for 3 seconds, wakes, checks the PIR sensor
- Every ~2 minutes: reads all sensors, connects WiFi, publishes MQTT, disconnects
- **Adaptive publishing**: skips MQTT if values haven't changed significantly, saving WiFi energy. Forces publish after 5 skipped cycles (~10 minutes max silence). Pressure uses a tight 0.1 hPa threshold so forecast-relevant changes report quickly.
- Display is off, WiFi is off between publishes

### Display-Active Mode (motion detected)
- MCU keeps the 3-second sleep cycles — the OLED is painted once and **retains the frame across deep sleeps** (OLEDs hold their state without drawing extra current)
- PIR HIGH on any wake refills the 60-second "display-on" countdown
- Sensors + display redraw every ~15 seconds while the display is lit
- MQTT stays on the normal 2-minute cadence — PIR events do not trigger extra publishes; the motion flag included in each publish reflects whether the display is currently active
- When the countdown reaches 0 the OLED is cleared and the device falls back to pure idle

### Low Battery Mode (< 3.5V)
- Skips WiFi, sensor reads, and active mode — all non-essential power draws are disabled
- Keeps waking every 3s to check the PIR so motion is still registered
- While motion is present, the full-screen "Low battery!" warning (large crossed-out battery icon + voltage) flashes — shown on one wake, hidden on the next, alternating for as long as the PIR keeps triggering
- When motion stops the display is cleared and the device just cycles PIR checks silently

### Features
- **Sea-level pressure**: raw BMP280 reading adjusted for 235m station altitude
- **Zambretti forecast**: weather prediction based on pressure value and trend with short Czech labels for the OLED (e.g., "Jasno", "Prehanky", "Bourky")
- **Trend arrows**: compares last 5 readings to show rising/falling/stable trends on the display
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
│     22.9 ↑   │     53.2 →   │
├──────────────┼──────────────┤
│ 1015 hPa ↓   │ [████] 3.87V │
│   Pekne       │              │
└──────────────┴──────────────┘
```

- **Top left**: Temperature with trend arrow
- **Top right**: Humidity with trend arrow
- **Bottom left**: Sea-level pressure with trend arrow + Zambretti weather forecast
- **Bottom right**: Battery icon (3.5-4.1V range) with voltage
