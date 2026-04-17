#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <credentials.h>
#include "sensors.h"
#include "display.h"
#include "mqtt.h"

// Deep sleep interval for idle mode (seconds)
#define IDLE_SLEEP_SECONDS   3

// Full sensor/MQTT cycle interval in idle mode (in wake counts)
#define FULL_CYCLE_INTERVAL  20   // ~60s at 3s sleep

// Active mode timings (ms)
#define ACTIVE_TIMEOUT_MS    60000 // Stay active 60s after last motion
#define PUBLISH_INTERVAL_MS  30000 // Publish every 30s while active
#define SENSOR_INTERVAL_MS   15000 // Read sensors every 15s while active

// Temperature calibration offset (°C)
#define TEMP_OFFSET          0.9

// WiFi
#define WIFI_TIMEOUT_MS      5000  // Shorter timeout with fast connect
#define WIFI_TX_POWER        10.0  // dBm (default 20.5, range 0-20.5)

// Battery voltage divider: 100kΩ from battery+ to A0
#define VBAT_MULTIPLIER      0.004007  // calibrated: 4.1V at ADC=1023

// PIR motion sensor
#define PIR_PIN              D6
#define LED_PIN              D4   // Wemos built-in LED (active LOW)

// RTC memory state (persists across deep sleep)
struct RtcState {
    uint32_t wakeCounter;
    float    temperature;
    float    humidity;
    float    pressure;
    float    altitude;
    float    bmpTemp;
    float    batteryVoltage;
    uint32_t dhtOk;
    uint32_t bmpOk;
    // WiFi fast connect cache
    uint8_t  bssid[6];
    int32_t  wifiChannel;
    uint32_t wifiCached;       // 1 if bssid/channel are valid
    uint32_t magic;
};

#define RTC_MAGIC 0xE5A70002
#define RTC_ADDR  0

const char* ssid     = STASSID;
const char* password = STAPSK;

RtcState rtcState;

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setOutputPower(WIFI_TX_POWER);

    if (rtcState.wifiCached) {
        // Fast connect: skip channel scan by providing BSSID and channel
        WiFi.begin(ssid, password, rtcState.wifiChannel, rtcState.bssid, true);
        Serial.print("[WIFI] Fast connect ch:");
        Serial.println(rtcState.wifiChannel);
    } else {
        WiFi.begin(ssid, password);
        Serial.println("[WIFI] Full scan connect");
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(50);
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Cache BSSID and channel for next fast connect
        memcpy(rtcState.bssid, WiFi.BSSID(), 6);
        rtcState.wifiChannel = WiFi.channel();
        rtcState.wifiCached  = 1;
        Serial.println("[WIFI] Connected in " + String(millis() - start) + "ms — IP: " + WiFi.localIP().toString());
    } else {
        // Fast connect failed — invalidate cache, will do full scan next time
        if (rtcState.wifiCached) {
            rtcState.wifiCached = 0;
            Serial.println("[WIFI] Fast connect failed, cache cleared");
        } else {
            Serial.println("[WIFI] Connection failed, skipping publish");
        }
    }
}

void disconnectWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

void publishMqtt(bool motionOn) {
    if (WiFi.status() != WL_CONNECTED) return;

    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

    String clientId = "ENV_SENSOR_" + String(ESP.getChipId(), HEX);
    bool connected;
    #ifdef MQTT_USER
        connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                                        MQTT_AVAILABLE_TOPIC, 0, true, "offline");
    #else
        connected = mqttClient.connect(clientId.c_str(),
                                        MQTT_AVAILABLE_TOPIC, 0, true, "offline");
    #endif

    if (!connected) {
        Serial.println("[MQTT] Connection failed, rc=" + String(mqttClient.state()));
        return;
    }

    char buf[16];

    if (sensorData.dhtOk) {
        dtostrf(sensorData.temperature, 4, 1, buf);
        mqttClient.publish(MQTT_TEMPERATURE_TOPIC, buf, true);
        dtostrf(sensorData.humidity, 4, 1, buf);
        mqttClient.publish(MQTT_HUMIDITY_TOPIC, buf, true);
    }

    if (sensorData.bmpOk) {
        dtostrf(sensorData.pressure, 6, 1, buf);
        mqttClient.publish(MQTT_PRESSURE_TOPIC, buf, true);
        dtostrf(sensorData.altitude, 6, 1, buf);
        mqttClient.publish(MQTT_ALTITUDE_TOPIC, buf, true);
    }

    dtostrf(rtcState.batteryVoltage, 4, 2, buf);
    mqttClient.publish(MQTT_BATTERY_TOPIC, buf, true);

    mqttClient.publish(MQTT_MOTION_TOPIC, motionOn ? "ON" : "OFF", true);
    mqttClient.publish(MQTT_AVAILABLE_TOPIC, "online", true);

    // Flush
    unsigned long start = millis();
    while (millis() - start < 100) {
        mqttClient.loop();
        delay(10);
    }

    mqttClient.disconnect();
    Serial.println("[MQTT] Published");
}

void cacheToRtc() {
    rtcState.temperature = sensorData.temperature;
    rtcState.humidity    = sensorData.humidity;
    rtcState.pressure    = sensorData.pressure;
    rtcState.altitude    = sensorData.altitude;
    rtcState.bmpTemp     = sensorData.bmpTemp;
    rtcState.dhtOk       = sensorData.dhtOk;
    rtcState.bmpOk       = sensorData.bmpOk;
}

void restoreFromRtc() {
    sensorData.temperature = rtcState.temperature;
    sensorData.humidity    = rtcState.humidity;
    sensorData.pressure    = rtcState.pressure;
    sensorData.altitude    = rtcState.altitude;
    sensorData.bmpTemp     = rtcState.bmpTemp;
    sensorData.dhtOk       = rtcState.dhtOk;
    sensorData.bmpOk       = rtcState.bmpOk;
}

void readBattery() {
    int rawAdc = analogRead(A0);
    rtcState.batteryVoltage = rawAdc * VBAT_MULTIPLIER;
    Serial.println("[BATT] " + String(rtcState.batteryVoltage, 2) + "V (ADC:" + String(rawAdc) + ")");
}

void fullSensorCycle() {
    readBattery();
    initiateSensors();
    readSensors(TEMP_OFFSET);
    cacheToRtc();
}

void publishCycle(bool motionOn) {
    connectWiFi();
    publishMqtt(motionOn);
    disconnectWiFi();
}

void saveAndSleep(uint32_t seconds) {
    ESP.rtcUserMemoryWrite(RTC_ADDR, (uint32_t*)&rtcState, sizeof(rtcState));
    Serial.flush();
    disconnectWiFi();
    ESP.deepSleep(seconds * 1000000UL);
}

// ── Active mode: stay awake, display on, periodic publish ──────────

void activeMode() {
    Serial.println("[MODE] Active — display on");

    // LED on briefly
    digitalWrite(LED_PIN, LOW);

    // Initial read + display
    fullSensorCycle();
    initiateDisplay();
    updateDisplay(rtcState.batteryVoltage);

    // First publish
    publishCycle(true);

    // LED off after first cycle
    digitalWrite(LED_PIN, HIGH);

    unsigned long lastMotion  = millis();
    unsigned long lastPublish = millis();
    unsigned long lastSensor  = millis();

    while (millis() - lastMotion < ACTIVE_TIMEOUT_MS) {
        // Check PIR continuously
        if (digitalRead(PIR_PIN) == HIGH) {
            lastMotion = millis();
        }

        unsigned long now = millis();

        // Periodic sensor read
        if (now - lastSensor >= SENSOR_INTERVAL_MS) {
            lastSensor = now;
            readSensors(TEMP_OFFSET);
            cacheToRtc();
            updateDisplay(rtcState.batteryVoltage);
        }

        // Periodic publish (WiFi on/off)
        if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
            lastPublish = now;
            readBattery();
            readSensors(TEMP_OFFSET);
            cacheToRtc();
            updateDisplay(rtcState.batteryVoltage);
            publishCycle(true);
        }

        delay(100);
        yield();
    }

    Serial.println("[MODE] PIR timeout — going idle");

    // Publish motion OFF before sleeping
    publishCycle(false);

    clearDisplay();
    rtcState.wakeCounter = 0;
    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Idle mode: deep sleep, 3s wakes, PIR check ────────────────────

void idleMode() {
    rtcState.wakeCounter++;
    bool fullCycle = rtcState.wakeCounter >= FULL_CYCLE_INTERVAL;

    if (fullCycle) {
        Serial.println("[MODE] Idle — full cycle");
        rtcState.wakeCounter = 0;

        fullSensorCycle();
        publishCycle(false);
    }

    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Entry point ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println();

    pinMode(PIR_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Load RTC state
    ESP.rtcUserMemoryRead(RTC_ADDR, (uint32_t*)&rtcState, sizeof(rtcState));
    if (rtcState.magic != RTC_MAGIC) {
        memset(&rtcState, 0, sizeof(rtcState));
        rtcState.magic = RTC_MAGIC;
        rtcState.wakeCounter = FULL_CYCLE_INTERVAL; // Force full cycle on first boot
    }

    bool motionDetected = digitalRead(PIR_PIN) == HIGH;

    if (motionDetected) {
        activeMode();
    } else {
        idleMode();
    }
}

void loop() {
    // Never reached — both modes end in deep sleep
}
