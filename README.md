# Environmental Sensor

Battery-powered environmental monitoring station built on a Wemos D1 Mini (ESP8266). Measures temperature, humidity, barometric pressure (sea-level adjusted), and detects motion. Uses deep sleep for long battery life, waking briefly to check sensors and publish data via MQTT with Home Assistant auto-discovery. A 1.3" OLED display with trend arrows shows live readings when motion is detected.

## Hardware

- **Wemos D1 Mini** (ESP8266)
- **DHT22** — temperature & humidity sensor
- **BMP180** — barometric pressure sensor (I2C)
- **1.3" 128x64 OLED** — SH1106 I2C display
- **PIR motion sensor** — triggers display and active mode
- **18650 Li-Ion battery** — via HT7330 3.3V LDO regulator

### Wiring

| Component          | Pin  | Wemos D1 Mini | GPIO | Notes                        |
|--------------------|------|---------------|------|------------------------------|
| Deep sleep         | D0   | RST           | 16   | Required for wake from sleep |
| BMP180             | SCL  | D1            | 5    | I2C clock (shared bus)       |
| BMP180             | SDA  | D2            | 4    | I2C data (shared bus)        |
| OLED               | SCL  | D1            | 5    | I2C clock (shared bus)       |
| OLED               | SDA  | D2            | 4    | I2C data (shared bus)        |
| DHT22              | DATA | D5            | 14   |                              |
| PIR sensor         | OUT  | D6            | 12   |                              |
| Battery (via 100kΩ)| +    | A0            |      | Voltage monitoring           |

- BMP180 and OLED share the I2C bus on D1/D2
- Power all sensors from 3.3V (HT7330 LDO output)
- D0 must be wired to RST for deep sleep wake
- 100kΩ resistor from battery+ to A0 for voltage monitoring

### Power Supply

```
18650 Battery (+) ──→ HT7330 VIN ──→ HT7330 VOUT (3.3V) ──→ Wemos 3V3 pin
18650 Battery (+) ──[100kΩ]──→ Wemos A0 (battery monitoring)
18650 Battery (-) ──→ GND
```

The HT7330 LDO regulates the battery (3.5-4.2V) to a stable 3.3V. Below 3.5V the device enters low battery mode — skipping WiFi and sleeping for 60 seconds between checks.

## Setup

1. Install required libraries via Arduino Library Manager:
   - `DHT sensor library` (Adafruit)
   - `Adafruit Unified Sensor`
   - `Adafruit BMP085 Library`
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
- Every ~60 seconds: reads all sensors, connects WiFi, publishes MQTT, disconnects
- **Adaptive publishing**: skips MQTT if values haven't changed significantly, saving WiFi energy. Forces publish after 5 skipped cycles (~5 minutes).
- Display is off, WiFi is off between publishes

### Active Mode (motion detected)
- Stays fully awake — OLED display shows live readings with **trend arrows** (↑↓→)
- Sensors read every 15 seconds, display updated
- WiFi connects every 30 seconds for MQTT publish, disconnects immediately
- LED blinks on initial motion detection
- Returns to idle after 60 seconds of no motion

### Low Battery Mode (< 3.5V)
- Skips WiFi and sensor reads to conserve remaining power
- Shows warning on OLED display
- Sleeps for 60 seconds between checks

### Features
- **Sea-level pressure**: raw BMP180 reading adjusted for 235m station altitude
- **Zambretti forecast**: weather prediction based on pressure value and trend with short Czech labels for the OLED (e.g., "Jasno", "Prehanky", "Bourky")
- **Trend arrows**: compares last 5 readings to show rising/falling/stable trends on the display
- **MQTT auto-discovery**: Home Assistant sensors appear automatically, no manual YAML needed
- **Watchdog timer**: 8-second hardware WDT prevents hangs
- **WiFi fast connect**: caches router BSSID/channel for ~1s connection time
- **Sensor calibration**: temperature offset (0.9°C) and humidity linear correction

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `environmental_sensor/temperature` | Publish | Temperature in °C |
| `environmental_sensor/humidity` | Publish | Relative humidity in % |
| `environmental_sensor/sea_level_pressure` | Publish | Sea-level pressure in hPa |
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
