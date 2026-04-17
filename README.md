# Environmental Sensor

Battery-powered environmental monitoring station built on a Wemos D1 Mini (ESP8266). Measures temperature, humidity, barometric pressure, and detects motion. Uses deep sleep for long battery life, waking briefly to check sensors and publish data via MQTT for Home Assistant integration. A 1.3" OLED display shows live readings when motion is detected.

## Hardware

- **Wemos D1 Mini** (ESP8266)
- **DHT22** — temperature & humidity sensor
- **BMP180** — barometric pressure sensor (I2C)
- **1.3" 128x64 OLED** — SH1106 I2C display
- **PIR motion sensor** — triggers display and active mode
- **18650 Li-Ion battery** — via HT7330 3.3V LDO regulator

### Wiring

| Component     | Pin  | Wemos D1 Mini | GPIO | Notes                        |
|---------------|------|---------------|------|------------------------------|
| Deep sleep    | D0   | RST           | 16   | Required for wake from sleep |
| BMP180        | SCL  | D1            | 5    | I2C clock (shared bus)       |
| BMP180        | SDA  | D2            | 4    | I2C data (shared bus)        |
| OLED          | SCL  | D1            | 5    | I2C clock (shared bus)       |
| OLED          | SDA  | D2            | 4    | I2C data (shared bus)        |
| DHT22         | DATA | D5            | 14   |                              |
| PIR sensor    | OUT  | D6            | 12   |                              |
| Battery (via 100kΩ) | +  | A0       |      | Voltage monitoring           |

- BMP180 and OLED share the I2C bus on D1/D2
- Power all sensors from 3.3V (HT7330 LDO output)
- D0 must be wired to RST for deep sleep wake
- 100kΩ resistor from battery+ to A0 for voltage monitoring (uses onboard 220k+100k divider)

### Power Supply

```
18650 Battery (+) ──→ HT7330 VIN ──→ HT7330 VOUT (3.3V) ──→ Wemos 3V3 pin
18650 Battery (+) ──[100kΩ]──→ Wemos A0 (battery monitoring)
18650 Battery (-) ──→ GND
```

The HT7330 LDO regulates the battery (3.5-4.2V) to a stable 3.3V. Below 3.5V the regulator cannot maintain output and the device becomes unstable — the battery icon reflects this as the empty threshold.

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

The device operates in two modes:

### Idle Mode (no motion)
- Deep sleeps for 3 seconds, wakes, checks the PIR sensor
- Every ~60 seconds: reads all sensors, connects WiFi, publishes MQTT, disconnects
- Display is off, WiFi is off between publishes
- Battery consumption: ~2-3mA average

### Active Mode (motion detected)
- Stays fully awake — OLED display shows live readings
- Sensors read every 15 seconds, display updated
- WiFi connects every 30 seconds for MQTT publish, disconnects immediately
- LED blinks on initial motion detection
- Returns to idle after 60 seconds of no motion

### Power Optimizations
- WiFi fast connect: caches router BSSID/channel in RTC memory (~1s vs ~3s)
- Reduced WiFi TX power (10 dBm)
- WiFi completely off when not publishing
- 3-second idle sleep balances PIR response vs. battery life

### Sensor Calibration
Temperature and humidity readings are calibrated:
- Temperature: 0.9°C offset subtracted (ESP8266 self-heating compensation)
- Humidity: linear calibration anchored at 100%, compensating for DHT22 sensor drift

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `environmental_sensor/temperature` | Publish | Temperature in °C |
| `environmental_sensor/humidity` | Publish | Relative humidity in % |
| `environmental_sensor/pressure` | Publish | Pressure in hPa |
| `environmental_sensor/altitude` | Publish | Altitude in m |
| `environmental_sensor/battery` | Publish | Battery voltage in V |
| `environmental_sensor/motion` | Publish | PIR state: ON / OFF |
| `environmental_sensor/available` | Publish | Online status (LWT) |

## Home Assistant Configuration

Add to `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Environmental Sensor Temperature"
      state_topic: "environmental_sensor/temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      availability_topic: "environmental_sensor/available"
      unique_id: "env_sensor_temperature"
      device:
        identifiers: ["environmental_sensor"]
        name: "Environmental Sensor"
        manufacturer: "DIY"
        model: "Wemos D1 Mini + DHT22 + BMP180"

    - name: "Environmental Sensor Humidity"
      state_topic: "environmental_sensor/humidity"
      unit_of_measurement: "%"
      device_class: humidity
      state_class: measurement
      availability_topic: "environmental_sensor/available"
      unique_id: "env_sensor_humidity"
      device:
        identifiers: ["environmental_sensor"]

    - name: "Environmental Sensor Pressure"
      state_topic: "environmental_sensor/pressure"
      unit_of_measurement: "hPa"
      device_class: atmospheric_pressure
      state_class: measurement
      availability_topic: "environmental_sensor/available"
      unique_id: "env_sensor_pressure"
      device:
        identifiers: ["environmental_sensor"]

    - name: "Environmental Sensor Altitude"
      state_topic: "environmental_sensor/altitude"
      unit_of_measurement: "m"
      icon: "mdi:altimeter"
      state_class: measurement
      availability_topic: "environmental_sensor/available"
      unique_id: "env_sensor_altitude"
      device:
        identifiers: ["environmental_sensor"]

    - name: "Environmental Sensor Battery"
      state_topic: "environmental_sensor/battery"
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability_topic: "environmental_sensor/available"
      unique_id: "env_sensor_battery"
      device:
        identifiers: ["environmental_sensor"]

  binary_sensor:
    - name: "Environmental Sensor Motion"
      state_topic: "environmental_sensor/motion"
      payload_on: "ON"
      payload_off: "OFF"
      device_class: motion
      availability_topic: "environmental_sensor/available"
      unique_id: "env_sensor_motion"
      off_delay: 30
      device:
        identifiers: ["environmental_sensor"]
```
