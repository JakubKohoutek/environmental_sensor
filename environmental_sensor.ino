#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <credentials.h>
#include "sensors.h"
#include "display.h"
#include "mqtt.h"
#include "debug.h"

// Trend direction (used internally for Zambretti forecast)
enum Trend { TREND_STABLE, TREND_UP, TREND_DOWN };

// Deep sleep interval for idle mode (seconds). Low-battery mode uses the
// same interval so PIR motion is still detected promptly — WiFi and
// sensor reads are skipped there, so the extra wakes cost very little.
#define IDLE_SLEEP_SECONDS   3

// Full sensor/MQTT cycle interval in idle mode (in wake counts)
#define FULL_CYCLE_INTERVAL  40   // ~120s at 3s sleep

// Display-active mode: PIR triggers the OLED on, but the MCU still sleeps
// 3s at a time between PIR polls. The OLED retains its last frame across
// deep-sleep cycles without drawing additional current.
#define DISPLAY_ON_WAKES         20   // Keep OLED lit 60s after last PIR HIGH
#define DISPLAY_REFRESH_INTERVAL 5    // Check sensors every 15s while lit

// Display redraw thresholds — only repaint when the rendered digits would
// actually change. `display.begin()` sends a DISPLAY_OFF→DISPLAY_ON pair
// that visibly flickers the OLED, so we avoid calling it on unchanged data.
#define DISP_TEMP_THRESHOLD      0.05f   // matches %.1f rendering
#define DISP_HUM_THRESHOLD       0.05f
#define DISP_BATT_THRESHOLD      0.005f  // matches %.2f rendering

// Temperature calibration offset
#define TEMP_OFFSET          0.3  // °C to subtract from DHT22 reading (calibration)

// WiFi
#define WIFI_TIMEOUT_MS      5000
#define WIFI_TX_POWER        10.0

// Battery voltage divider: 100k from battery+ to A0
#define VBAT_MULTIPLIER      0.004007
#define VBAT_LOW             3.5   // Below this, skip WiFi and warn

// PIR motion sensor
#define PIR_PIN              D6
#define LED_PIN              D4

// Adaptive publish thresholds — skip publish if all values within these.
// Pressure threshold matches the published precision (0.1 hPa) so any visible
// change is reported promptly — atmospheric pressure changes are small but
// meaningful for the Zambretti forecast.
#define TEMP_THRESHOLD       0.2   // °C
#define HUM_THRESHOLD        1.0   // %
#define PRES_THRESHOLD       0.1   // hPa
#define BATT_THRESHOLD       0.05  // V
#define MAX_SKIP_CYCLES      5     // Force publish after this many skipped cycles

// Trend history
#define TREND_HISTORY_SIZE       5
#define TREND_TEMP_THRESHOLD     0.3   // °C change to register a trend
#define TREND_HUM_THRESHOLD      1.5   // %
#define TREND_PRES_THRESHOLD     0.5   // hPa
// recordHistory is called on every full cycle (idle, ~2 min) and multiple
// times per active-mode cycle. Zambretti expects a multi-hour pressure
// trend, so only commit every Nth call to the circular buffer — spans
// ~5×18×2min ≈ 3 hours worth of idle-mode samples.
#define TREND_SUBSAMPLE_INTERVAL 18

// How many full cycles between MQTT discovery republishes. Home Assistant
// may restart and lose entities; rebroadcasting keeps them registered.
// ~30 cycles × 2 min = ~60 min.
#define DISCOVERY_REPUBLISH_INTERVAL 30

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
    // Trend history (circular buffer) — only presHistory is used (for
    // Zambretti forecast). Temp/hum history arrays are not read anywhere.
    float    presHistory[TREND_HISTORY_SIZE];
    uint32_t historyIndex;
    uint32_t historyCount;
    uint32_t trendSubsampleCounter;
    // MQTT discovery
    uint32_t discoveryPublished;
    uint32_t cyclesSinceDiscovery;
    // Low-battery warning flash state — toggled each wake while PIR is HIGH
    // so the warning blinks (shown one cycle, hidden the next). Cleared
    // back to 0 whenever PIR returns LOW so the display goes dark.
    uint32_t lowBatteryWarningShown;
    // Display-active state: wakes remaining before OLED is cleared, and
    // wake countdown until the next sensor/display redraw.
    uint32_t displayOnCountdown;
    uint32_t displayRefreshDue;
    // Last values actually rendered to the OLED. Used to skip redraws
    // when nothing visible has changed, preventing display flicker.
    float    lastDispTemp;
    float    lastDispHum;
    float    lastDispBatt;
    uint32_t lastDispDhtOk;
    char     lastDispForecast[10];  // max 9 chars + null
    uint32_t magic;
};

#define RTC_MAGIC 0xE5A70007
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
        DBG_PRINT("[WIFI] Fast connect ch:");
        DBG_PRINTLN(rtcState.wifiChannel);
    } else {
        WiFi.begin(ssid, password);
        DBG_PRINTLN("[WIFI] Full scan connect");
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
        DBG_PRINTLN("[WIFI] Connected in " + String(millis() - start) + "ms — IP: " + WiFi.localIP().toString());
    } else {
        if (rtcState.wifiCached) {
            rtcState.wifiCached = 0;
            DBG_PRINTLN("[WIFI] Fast connect failed, cache cleared");
        } else {
            DBG_PRINTLN("[WIFI] Connection failed, skipping publish");
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
        {"sensor", "pressure",    "Pressure",           MQTT_PRESSURE_TOPIC,     "atmospheric_pressure", "hPa",         NULL, "measurement"},
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

    DBG_PRINTLN("[MQTT] Discovery published");
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
        DBG_PRINTLN("[MQTT] Connection failed, rc=" + String(mqttClient.state()));
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

    // Publish discovery on first connect and periodically after, so HA
    // re-registers entities if it restarted or lost them.
    rtcState.cyclesSinceDiscovery++;
    if (!rtcState.discoveryPublished ||
        rtcState.cyclesSinceDiscovery >= DISCOVERY_REPUBLISH_INTERVAL) {
        publishDiscovery();
        rtcState.discoveryPublished = 1;
        rtcState.cyclesSinceDiscovery = 0;
    }

    char buf[16];

    if (sensorData.dhtOk) {
        dtostrf(sensorData.temperature, 4, 1, buf);
        mqttClient.publish(MQTT_TEMPERATURE_TOPIC, buf, true);
        dtostrf(sensorData.humidity, 4, 1, buf);
        mqttClient.publish(MQTT_HUMIDITY_TOPIC, buf, true);
        rtcState.lastPubTemp = sensorData.temperature;
        rtcState.lastPubHum  = sensorData.humidity;
    }

    if (sensorData.bmpOk) {
        dtostrf(sensorData.seaLevelPressure, 6, 1, buf);
        mqttClient.publish(MQTT_PRESSURE_TOPIC, buf, true);
        dtostrf(sensorData.altitude, 6, 1, buf);
        mqttClient.publish(MQTT_ALTITUDE_TOPIC, buf, true);
        rtcState.lastPubPres = sensorData.seaLevelPressure;
    }

    dtostrf(rtcState.batteryVoltage, 4, 2, buf);
    mqttClient.publish(MQTT_BATTERY_TOPIC, buf, true);
    rtcState.lastPubBatt = rtcState.batteryVoltage;

    mqttClient.publish(MQTT_MOTION_TOPIC, motionOn ? "ON" : "OFF", true);
    mqttClient.publish(MQTT_AVAILABLE_TOPIC, "online", true);

    flushMqtt();

    rtcState.skipCount = 0;

    mqttClient.disconnect();
    DBG_PRINTLN("[MQTT] Published");
}

// ── Adaptive publish ──────────────────────────────────────────────

bool shouldPublish() {
    if (rtcState.skipCount >= MAX_SKIP_CYCLES) return true;

    if (fabs(sensorData.temperature - rtcState.lastPubTemp) >= TEMP_THRESHOLD) return true;
    if (fabs(sensorData.humidity - rtcState.lastPubHum) >= HUM_THRESHOLD) return true;
    if (fabs(sensorData.seaLevelPressure - rtcState.lastPubPres) >= PRES_THRESHOLD) return true;
    if (fabs(rtcState.batteryVoltage - rtcState.lastPubBatt) >= BATT_THRESHOLD) return true;

    return false;
}

// ── Trend tracking ────────────────────────────────────────────────

void recordHistory() {
    rtcState.trendSubsampleCounter++;
    if (rtcState.trendSubsampleCounter % TREND_SUBSAMPLE_INTERVAL != 0) return;
    if (!sensorData.bmpOk) return;

    uint32_t idx = rtcState.historyIndex % TREND_HISTORY_SIZE;
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
    // Only persist fresh values — when a sensor fails the read returns 0
    // and we'd otherwise clobber the last-known-good cache, corrupting the
    // display forecast and the adaptive-publish comparison baseline.
    if (sensorData.dhtOk) {
        rtcState.temperature = sensorData.temperature;
        rtcState.humidity    = sensorData.humidity;
    }
    if (sensorData.bmpOk) {
        rtcState.pressure         = sensorData.pressure;
        rtcState.seaLevelPressure = sensorData.seaLevelPressure;
        rtcState.altitude         = sensorData.altitude;
        rtcState.bmpTemp          = sensorData.bmpTemp;
    }
    rtcState.dhtOk = sensorData.dhtOk;
    rtcState.bmpOk = sensorData.bmpOk;
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
    // Average several samples — a single ADC read near the low-battery
    // threshold can be noisy enough to spuriously trip lowBatteryMode.
    const int samples = 2;
    int total = 0;
    for (int i = 0; i < samples; i++) {
        total += analogRead(A0);
        delay(2);
    }
    int rawAdc = total / samples;
    rtcState.batteryVoltage = rawAdc * VBAT_MULTIPLIER;
    DBG_PRINTLN("[BATT] " + String(rtcState.batteryVoltage, 2) + "V (ADC:" + String(rawAdc) + ")");
}

void fullSensorCycle() {
    readBattery();
    initiateSensors();
    readSensors(TEMP_OFFSET);
    cacheToRtc();
    recordHistory();
}

Trend presTrend()  { return getTrend(rtcState.presHistory, rtcState.historyCount, rtcState.historyIndex, TREND_PRES_THRESHOLD); }

int presTrendInt() {
    Trend t = presTrend();
    if (t == TREND_UP) return 1;
    if (t == TREND_DOWN) return -1;
    return 0;
}

const char* forecast() {
    return zambretti(rtcState.seaLevelPressure, presTrendInt());
}

void publishCycle(bool motionOn) {
    connectWiFi();
    publishSensorData(motionOn);
    disconnectWiFi();
}

void saveAndSleep(uint32_t seconds) {
    ESP.rtcUserMemoryWrite(RTC_ADDR, (uint32_t*)&rtcState, sizeof(rtcState));
    DBG_FLUSH();
    disconnectWiFi();
    // WAKE_NO_RFCAL skips the ~75 mA × ~200 ms RF calibration burst on
    // wake (uses cached calibration data instead). RF is still available
    // for WiFi — only the per-wake cal is skipped. Much less power on
    // quick PIR-poll wakes, and should keep the ADC bandgap stable since
    // calibration isn't running each boot.
    ESP.deepSleep(seconds * 1000000UL, WAKE_NO_RFCAL);
}

// ── Low battery mode ──────────────────────────────────────────────

void lowBatteryMode() {
    bool pirHigh = digitalRead(PIR_PIN) == HIGH;

    if (pirHigh) {
        // While motion is held, flash the warning: show it on one wake,
        // hide it on the next. The OLED retains whatever we last sent to
        // it across the deep-sleep interval, so no on-CPU delay is needed.
        if (rtcState.lowBatteryWarningShown) {
            clearDisplay();
            rtcState.lowBatteryWarningShown = 0;
        } else {
            initiateDisplay();
            showLowBatteryWarning(rtcState.batteryVoltage);
            rtcState.lowBatteryWarningShown = 1;
        }
    } else if (rtcState.lowBatteryWarningShown) {
        // Motion ended — turn the display off and reset the flash state
        // so the next motion event starts the cycle from "shown".
        clearDisplay();
        rtcState.lowBatteryWarningShown = 0;
    }

    DBG_PRINTLN("[BATT] LOW " + String(rtcState.batteryVoltage, 2) +
                   "V — PIR " + (pirHigh ? "HIGH" : "LOW") +
                   ", warning " + (rtcState.lowBatteryWarningShown ? "ON" : "OFF"));

    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Display-active wake ───────────────────────────────────────────

// Called on every wake while the display-on countdown is active. Handles
// one iteration (refresh sensors + redraw if due, publish on the normal
// full-cycle schedule, decrement countdowns, clear OLED on timeout) and
// returns to deep sleep. The OLED retains whatever frame was last drawn
// across the 3s sleep cycles without additional current draw.
void displayActiveWake(bool firstPirDetection) {
    rtcState.wakeCounter++;

    bool refresh = firstPirDetection || rtcState.displayRefreshDue == 0;
    if (refresh) {
        fullSensorCycle();
        rtcState.displayRefreshDue = DISPLAY_REFRESH_INTERVAL;

        // Only repaint the OLED when something visible has changed —
        // `display.begin()` inside initiateDisplay() flickers the panel,
        // so we avoid calling it when the rendered frame would be identical.
        const char* currentForecast = forecast();
        bool needRedraw = firstPirDetection ||
            ((bool)sensorData.dhtOk != (bool)rtcState.lastDispDhtOk) ||
            (sensorData.dhtOk && (
                fabs(sensorData.temperature - rtcState.lastDispTemp) >= DISP_TEMP_THRESHOLD ||
                fabs(sensorData.humidity    - rtcState.lastDispHum)  >= DISP_HUM_THRESHOLD)) ||
            fabs(rtcState.batteryVoltage - rtcState.lastDispBatt) >= DISP_BATT_THRESHOLD ||
            strcmp(currentForecast, rtcState.lastDispForecast) != 0;

        if (needRedraw) {
            DBG_PRINTLN(firstPirDetection ? "[MODE] PIR — display on" : "[MODE] Display refresh");
            initiateDisplay();
            updateDisplay(rtcState.batteryVoltage, currentForecast);
            rtcState.lastDispTemp  = sensorData.temperature;
            rtcState.lastDispHum   = sensorData.humidity;
            rtcState.lastDispBatt  = rtcState.batteryVoltage;
            rtcState.lastDispDhtOk = sensorData.dhtOk;
            strncpy(rtcState.lastDispForecast, currentForecast,
                    sizeof(rtcState.lastDispForecast) - 1);
            rtcState.lastDispForecast[sizeof(rtcState.lastDispForecast) - 1] = '\0';
        } else {
            DBG_PRINTLN("[MODE] Display unchanged — skipping redraw");
        }
    }

    // MQTT publish stays on the normal 2-min idle cadence — no extra
    // publishes just because the display is active. Motion flag reflects
    // the current display-active state.
    if (rtcState.wakeCounter >= FULL_CYCLE_INTERVAL) {
        DBG_PRINTLN("[MODE] Active — full cycle");
        rtcState.wakeCounter = 0;
        if (!refresh) {
            // Sensors weren't refreshed this wake; read now so the publish
            // carries current values.
            fullSensorCycle();
        }
        if (shouldPublish()) {
            publishCycle(true);
        } else {
            rtcState.skipCount++;
            DBG_PRINTLN("[MQTT] Skipped — values unchanged (" + String(rtcState.skipCount) + "/" + String(MAX_SKIP_CYCLES) + ")");
        }
    }

    rtcState.displayOnCountdown--;
    if (rtcState.displayRefreshDue > 0) rtcState.displayRefreshDue--;

    if (rtcState.displayOnCountdown == 0) {
        DBG_PRINTLN("[MODE] PIR timeout — display off");
        clearDisplay();
        // Invalidate the display cache so the next PIR wake repaints.
        rtcState.lastDispForecast[0] = '\0';
    }

    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Idle mode ─────────────────────────────────────────────────────

void idleMode() {
    rtcState.wakeCounter++;
    bool fullCycle = rtcState.wakeCounter >= FULL_CYCLE_INTERVAL;

    if (fullCycle) {
        DBG_PRINTLN("[MODE] Idle — full cycle");
        rtcState.wakeCounter = 0;

        fullSensorCycle();

        if (shouldPublish()) {
            publishCycle(false);
        } else {
            rtcState.skipCount++;
            DBG_PRINTLN("[MQTT] Skipped — values unchanged (" + String(rtcState.skipCount) + "/" + String(MAX_SKIP_CYCLES) + ")");
        }
    }

    saveAndSleep(IDLE_SLEEP_SECONDS);
}

// ── Entry point ───────────────────────────────────────────────────

void setup() {
    // 74880 matches the ESP8266 ROM bootloader baud, so the boot message
    // on every wake is readable instead of garbage in the serial monitor.
    DBG_BEGIN(74880);
    DBG_NEWLINE();

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

    bool pirHigh = digitalRead(PIR_PIN) == HIGH;
    bool firstPirDetection = false;

    // PIR HIGH (re)arms the display-on countdown. Track whether this wake
    // is the transition from display-off to display-on so the handler
    // knows to do an initial refresh + OLED init.
    if (pirHigh) {
        firstPirDetection = (rtcState.displayOnCountdown == 0);
        rtcState.displayOnCountdown = DISPLAY_ON_WAKES;
    }

    if (rtcState.displayOnCountdown > 0) {
        displayActiveWake(firstPirDetection);
    } else {
        idleMode();
    }
}

void loop() {
    // Never reached — both modes end in deep sleep
}
