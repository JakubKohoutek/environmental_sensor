#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <credentials.h>
#include "sensors.h"
#include "display.h"
#include "mqtt.h"

// Deep sleep interval for idle mode (seconds)
#define IDLE_SLEEP_SECONDS   3
#define LOW_BATT_SLEEP_SECONDS 60

// Full sensor/MQTT cycle interval in idle mode (in wake counts)
#define FULL_CYCLE_INTERVAL  20   // ~60s at 3s sleep

// Active mode timings (ms)
#define ACTIVE_TIMEOUT_MS    60000 // Stay active 60s after last motion
#define PUBLISH_INTERVAL_MS  30000 // Publish every 30s while active
#define SENSOR_INTERVAL_MS   15000 // Read sensors every 15s while active

// Temperature calibration offset
#define TEMP_OFFSET          0.9

// WiFi
#define WIFI_TIMEOUT_MS      5000
#define WIFI_TX_POWER        10.0

// Battery voltage divider: 100k from battery+ to A0
#define VBAT_MULTIPLIER      0.004007
#define VBAT_LOW             3.5   // Below this, skip WiFi and warn

// PIR motion sensor
#define PIR_PIN              D6
#define LED_PIN              D4

// Adaptive publish thresholds — skip publish if all values within these
#define TEMP_THRESHOLD       0.2   // °C
#define HUM_THRESHOLD        1.0   // %
#define PRES_THRESHOLD       0.5   // hPa
#define BATT_THRESHOLD       0.05  // V
#define MAX_SKIP_CYCLES      5     // Force publish after this many skipped cycles

// Trend history
#define TREND_HISTORY_SIZE   5
#define TREND_TEMP_THRESHOLD 0.3   // °C change to register a trend
#define TREND_HUM_THRESHOLD  1.5   // %
#define TREND_PRES_THRESHOLD 0.5   // hPa

// ── RTC memory state ──────────────────────────────────────────────

struct RtcState {
    uint32_t wakeCounter;
    float    temperature;
    float    humidity;
    float    pressure;
    float    seaLevelPressure;
    float    altitude;
    float    bmpTemp;
    float    batteryVoltage;
    uint32_t dhtOk;
    uint32_t bmpOk;
    // WiFi fast connect cache
    uint8_t  bssid[6];
    int32_t  wifiChannel;
    uint32_t wifiCached;
    // Adaptive publish
    float    lastPubTemp;
    float    lastPubHum;
    float    lastPubPres;
    float    lastPubBatt;
    uint32_t skipCount;
    // Trend history (circular buffer)
    float    tempHistory[TREND_HISTORY_SIZE];
    float    humHistory[TREND_HISTORY_SIZE];
    float    presHistory[TREND_HISTORY_SIZE];
    uint32_t historyIndex;
    uint32_t historyCount;
    // MQTT discovery
    uint32_t discoveryPublished;
    uint32_t magic;
};

#define RTC_MAGIC 0xE5A70003
#define RTC_ADDR  0

const char* ssid     = STASSID;
const char* password = STAPSK;

RtcState rtcState;

// ── WiFi ──────────────────────────────────────────────────────────

void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setOutputPower(WIFI_TX_POWER);

    if (rtcState.wifiCached) {
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
        ESP.wdtFeed();
    }

    if (WiFi.status() == WL_CONNECTED) {
        memcpy(rtcState.bssid, WiFi.BSSID(), 6);
        rtcState.wifiChannel = WiFi.channel();
        rtcState.wifiCached  = 1;
        Serial.println("[WIFI] Connected in " + String(millis() - start) + "ms — IP: " + WiFi.localIP().toString());
    } else {
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

// ── MQTT Discovery ────────────────────────────────────────────────

void publishDiscovery() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!mqttClient.connected()) return;

    const char* deviceJson =
        "\"dev\":{\"ids\":[\"environmental_sensor\"],"
        "\"name\":\"Environmental Sensor\","
        "\"mf\":\"DIY\","
        "\"mdl\":\"Wemos D1 Mini + DHT22 + BMP180\"}";

    const char* avail =
        "\"avty_t\":\"" MQTT_AVAILABLE_TOPIC "\"";

    struct DiscoveryEntry {
        const char* component;  // "sensor" or "binary_sensor"
        const char* id;
        const char* name;
        const char* stateTopic;
        const char* deviceClass;
        const char* unit;
        const char* icon;       // NULL if deviceClass is set
        const char* stateClass; // NULL for binary_sensor
    };

    DiscoveryEntry entries[] = {
        {"sensor", "temperature", "Temperature",       MQTT_TEMPERATURE_TOPIC,  "temperature",          "\xc2\xb0""C", NULL, "measurement"},
        {"sensor", "humidity",    "Humidity",           MQTT_HUMIDITY_TOPIC,     "humidity",             "%",           NULL, "measurement"},
        {"sensor", "pressure",    "Pressure",           MQTT_SEA_PRESSURE_TOPIC, "atmospheric_pressure", "hPa",         NULL, "measurement"},
        {"sensor", "altitude",    "Altitude",           MQTT_ALTITUDE_TOPIC,     NULL,                   "m",           "mdi:altimeter", "measurement"},
        {"sensor", "battery",     "Battery",            MQTT_BATTERY_TOPIC,      "voltage",              "V",           NULL, "measurement"},
        {"binary_sensor", "motion", "Motion",           MQTT_MOTION_TOPIC,       "motion",               NULL,          NULL, NULL},
    };

    int numEntries = sizeof(entries) / sizeof(entries[0]);
    char topic[128];
    char payload[512];

    for (int i = 0; i < numEntries; i++) {
        DiscoveryEntry& e = entries[i];
        snprintf(topic, sizeof(topic),
            "homeassistant/%s/environmental_sensor/%s/config",
            e.component, e.id);

        int len = 0;
        len += snprintf(payload + len, sizeof(payload) - len,
            "{\"name\":\"%s\",\"stat_t\":\"%s\",\"uniq_id\":\"env_sensor_%s\",%s,%s",
            e.name, e.stateTopic, e.id, avail, deviceJson);

        if (e.unit) {
            len += snprintf(payload + len, sizeof(payload) - len, ",\"unit_of_meas\":\"%s\"", e.unit);
        }
        if (e.deviceClass) {
            len += snprintf(payload + len, sizeof(payload) - len, ",\"dev_cla\":\"%s\"", e.deviceClass);
        }
        if (e.icon) {
            len += snprintf(payload + len, sizeof(payload) - len, ",\"ic\":\"%s\"", e.icon);
        }
        if (e.stateClass) {
            len += snprintf(payload + len, sizeof(payload) - len, ",\"stat_cla\":\"%s\"", e.stateClass);
        }
        if (strcmp(e.component, "binary_sensor") == 0) {
            len += snprintf(payload + len, sizeof(payload) - len, ",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"off_dly\":30");
        }
        snprintf(payload + len, sizeof(payload) - len, "}");

        mqttClient.publish(topic, payload, true);
    }

    Serial.println("[MQTT] Discovery published");
}

// ── MQTT Publish ──────────────────────────────────────────────────

bool connectMqtt() {
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
    }
    return connected;
}

void flushMqtt() {
    unsigned long start = millis();
    while (millis() - start < 100) {
        mqttClient.loop();
        delay(10);
        ESP.wdtFeed();
    }
}

void publishSensorData(bool motionOn) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!connectMqtt()) return;

    // Publish discovery on first connect
    if (!rtcState.discoveryPublished) {
        publishDiscovery();
        rtcState.discoveryPublished = 1;
    }

    char buf[16];

    if (sensorData.dhtOk) {
        dtostrf(sensorData.temperature, 4, 1, buf);
        mqttClient.publish(MQTT_TEMPERATURE_TOPIC, buf, true);
        dtostrf(sensorData.humidity, 4, 1, buf);
        mqttClient.publish(MQTT_HUMIDITY_TOPIC, buf, true);
    }

    if (sensorData.bmpOk) {
        dtostrf(sensorData.seaLevelPressure, 6, 1, buf);
        mqttClient.publish(MQTT_SEA_PRESSURE_TOPIC, buf, true);
        dtostrf(sensorData.altitude, 6, 1, buf);
        mqttClient.publish(MQTT_ALTITUDE_TOPIC, buf, true);
    }

    dtostrf(rtcState.batteryVoltage, 4, 2, buf);
    mqttClient.publish(MQTT_BATTERY_TOPIC, buf, true);

    mqttClient.publish(MQTT_MOTION_TOPIC, motionOn ? "ON" : "OFF", true);
    mqttClient.publish(MQTT_AVAILABLE_TOPIC, "online", true);

    flushMqtt();

    // Update last published values
    rtcState.lastPubTemp = sensorData.temperature;
    rtcState.lastPubHum  = sensorData.humidity;
    rtcState.lastPubPres = sensorData.seaLevelPressure;
    rtcState.lastPubBatt = rtcState.batteryVoltage;
    rtcState.skipCount   = 0;

    mqttClient.disconnect();
    Serial.println("[MQTT] Published");
}

// ── Adaptive publish ──────────────────────────────────────────────

bool shouldPublish() {
    if (rtcState.skipCount >= MAX_SKIP_CYCLES) return true;

    if (abs(sensorData.temperature - rtcState.lastPubTemp) >= TEMP_THRESHOLD) return true;
    if (abs(sensorData.humidity - rtcState.lastPubHum) >= HUM_THRESHOLD) return true;
    if (abs(sensorData.seaLevelPressure - rtcState.lastPubPres) >= PRES_THRESHOLD) return true;
    if (abs(rtcState.batteryVoltage - rtcState.lastPubBatt) >= BATT_THRESHOLD) return true;

    return false;
}

// ── Trend tracking ────────────────────────────────────────────────

void recordHistory() {
    uint32_t idx = rtcState.historyIndex % TREND_HISTORY_SIZE;
    rtcState.tempHistory[idx] = sensorData.temperature;
    rtcState.humHistory[idx]  = sensorData.humidity;
    rtcState.presHistory[idx] = sensorData.seaLevelPressure;
    rtcState.historyIndex++;
    if (rtcState.historyCount < TREND_HISTORY_SIZE) {
        rtcState.historyCount++;
    }
}

Trend getTrend(float* history, uint32_t count, uint32_t currentIdx, float threshold) {
    if (count < 2) return TREND_STABLE;

    // Compare current (most recent) to oldest in buffer
    uint32_t newestIdx = (currentIdx - 1) % TREND_HISTORY_SIZE;
    uint32_t oldestIdx = (count < TREND_HISTORY_SIZE) ? 0 : (currentIdx % TREND_HISTORY_SIZE);
    float diff = history[newestIdx] - history[oldestIdx];

    if (diff > threshold) return TREND_UP;
    if (diff < -threshold) return TREND_DOWN;
    return TREND_STABLE;
}

// ── Helpers ───────────────────────────────────────────────────────

void cacheToRtc() {
    rtcState.temperature      = sensorData.temperature;
    rtcState.humidity         = sensorData.humidity;
    rtcState.pressure         = sensorData.pressure;
    rtcState.seaLevelPressure = sensorData.seaLevelPressure;
    rtcState.altitude         = sensorData.altitude;
    rtcState.bmpTemp          = sensorData.bmpTemp;
    rtcState.dhtOk            = sensorData.dhtOk;
    rtcState.bmpOk            = sensorData.bmpOk;
}

void restoreFromRtc() {
    sensorData.temperature      = rtcState.temperature;
    sensorData.humidity         = rtcState.humidity;
    sensorData.pressure         = rtcState.pressure;
    sensorData.seaLevelPressure = rtcState.seaLevelPressure;
    sensorData.altitude         = rtcState.altitude;
    sensorData.bmpTemp          = rtcState.bmpTemp;
    sensorData.dhtOk            = rtcState.dhtOk;
    sensorData.bmpOk            = rtcState.bmpOk;
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
    recordHistory();
}

Trend tempTrend()  { return getTrend(rtcState.tempHistory, rtcState.historyCount, rtcState.historyIndex, TREND_TEMP_THRESHOLD); }
Trend humTrend()   { return getTrend(rtcState.humHistory,  rtcState.historyCount, rtcState.historyIndex, TREND_HUM_THRESHOLD); }
Trend presTrend()  { return getTrend(rtcState.presHistory, rtcState.historyCount, rtcState.historyIndex, TREND_PRES_THRESHOLD); }

void publishCycle(bool motionOn) {
    connectWiFi();
    publishSensorData(motionOn);
    disconnectWiFi();
}

void saveAndSleep(uint32_t seconds) {
    ESP.rtcUserMemoryWrite(RTC_ADDR, (uint32_t*)&rtcState, sizeof(rtcState));
    Serial.flush();
    disconnectWiFi();
    ESP.deepSleep(seconds * 1000000UL);
}

// ── Low battery mode ──────────────────────────────────────────────

void lowBatteryMode() {
    Serial.println("[BATT] LOW — skipping WiFi, sleeping " + String(LOW_BATT_SLEEP_SECONDS) + "s");

    // Show low battery warning on display
    initiateDisplay();
    sensorData.dhtOk = false;
    sensorData.bmpOk = false;
    updateDisplay(rtcState.batteryVoltage);

    saveAndSleep(LOW_BATT_SLEEP_SECONDS);
}

// ── Active mode ───────────────────────────────────────────────────

void activeMode() {
    Serial.println("[MODE] Active — display on");

    digitalWrite(LED_PIN, LOW); // LED on

    fullSensorCycle();
    initiateDisplay();
    updateDisplay(rtcState.batteryVoltage, tempTrend(), humTrend(), presTrend());

    publishCycle(true);

    digitalWrite(LED_PIN, HIGH); // LED off

    unsigned long lastMotion  = millis();
    unsigned long lastPublish = millis();
    unsigned long lastSensor  = millis();

    while (millis() - lastMotion < ACTIVE_TIMEOUT_MS) {
        ESP.wdtFeed();

        if (digitalRead(PIR_PIN) == HIGH) {
            lastMotion = millis();
        }

        unsigned long now = millis();

        // Periodic sensor read
        if (now - lastSensor >= SENSOR_INTERVAL_MS) {
            lastSensor = now;
            readSensors(TEMP_OFFSET);
            cacheToRtc();
            recordHistory();
            updateDisplay(rtcState.batteryVoltage, tempTrend(), humTrend(), presTrend());
        }

        // Periodic publish (WiFi on/off)
        if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
            lastPublish = now;
            readBattery();
            readSensors(TEMP_OFFSET);
            cacheToRtc();
            recordHistory();
            updateDisplay(rtcState.batteryVoltage, tempTrend(), humTrend(), presTrend());
            publishCycle(true);
        }

        delay(100);
        yield();
    }

    Serial.println("[MODE] PIR timeout — going idle");

    publishCycle(false);

    clearDisplay();
    rtcState.wakeCounter = 0;
    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Idle mode ─────────────────────────────────────────────────────

void idleMode() {
    rtcState.wakeCounter++;
    bool fullCycle = rtcState.wakeCounter >= FULL_CYCLE_INTERVAL;

    if (fullCycle) {
        Serial.println("[MODE] Idle — full cycle");
        rtcState.wakeCounter = 0;

        fullSensorCycle();

        if (shouldPublish()) {
            publishCycle(false);
        } else {
            rtcState.skipCount++;
            Serial.println("[MQTT] Skipped — values unchanged (" + String(rtcState.skipCount) + "/" + String(MAX_SKIP_CYCLES) + ")");
        }
    }

    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Entry point ───────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println();

    // Enable watchdog timer
    ESP.wdtEnable(WDTO_8S);

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

    // Check battery early
    readBattery();
    if (rtcState.batteryVoltage < VBAT_LOW) {
        lowBatteryMode(); // Never returns — sleeps
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
