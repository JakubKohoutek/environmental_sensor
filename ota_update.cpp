#include <ArduinoOTA.h>
#include <credentials.h>

#include "display.h"
#include "debug.h"
#include "mqtt.h"
#include "ota_update.h"

namespace {
constexpr unsigned long REQUEST_TIMEOUT_MS = 1000;
bool requested = false;
bool cleared = false;
bool failed = false;

/** Records commands only; network writes occur outside the MQTT callback. */
void receiveCommand(char* topic, uint8_t* payload, unsigned int length) {
    if (strcmp(topic, MQTT_OTA_COMMAND_TOPIC) != 0) return;
    if (length == 5 && memcmp(payload, "START", 5) == 0) requested = true;
    if (length == 0) cleared = true;
}

void publishStatus(const char* status) {
    DBG_PRINT("[OTA] ");
    DBG_PRINTLN(status);
    mqttClient.publish(MQTT_OTA_STATUS_TOPIC, status, true);
    mqttClient.loop();
    ESP.wdtFeed();
}

/** Waits briefly for a retained command or the broker's echo of its removal. */
void waitForMessage(const bool& received) {
    unsigned long start = millis();
    while (!received && mqttClient.connected() && millis() - start < REQUEST_TIMEOUT_MS) {
        mqttClient.loop();
        delay(10);
        ESP.wdtFeed();
    }
}

/** Keeps the MCU awake for uploads; ArduinoOTA handles an active transfer synchronously. */
void waitForUpload() {
    failed = false;
    ArduinoOTA.setHostname("environmental-sensor");
    #ifdef OTA_PASSWORD
        ArduinoOTA.setPassword(OTA_PASSWORD);
    #endif
    ArduinoOTA.onStart([]() {
        showOtaStatus("Updating firmware", "Starting...", "Do not power off");
        // ESP8266 core 3.0.2 closes all TCP clients before this callback.
        if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED && connectMqtt()) {
            mqttClient.publish(MQTT_AVAILABLE_TOPIC, "online", true);
        }
        publishStatus("updating");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPercent = -1;
        int percent = total ? static_cast<int>(progress * 100UL / total) : 0;
        if (percent != lastPercent) {
            char text[16];
            snprintf(text, sizeof(text), "%d%%", percent);
            showOtaStatus("Updating firmware", text, "Do not power off");
            lastPercent = percent;
        }
        mqttClient.loop();
        ESP.wdtFeed();
    });
    ArduinoOTA.onEnd([]() {
        showOtaStatus("OTA complete", "Rebooting...", "");
        publishStatus("complete");
        mqttClient.disconnect();
    });
    ArduinoOTA.onError([](ota_error_t error) {
        DBG_PRINT("[OTA] Error code: ");
        DBG_PRINTLN(error);
        (void)error;
        failed = true;
        showOtaStatus("OTA failed", "Retry OTA request", "");
        publishStatus("error");
    });

    ArduinoOTA.begin();
    initiateDisplay();
    publishStatus("waiting");
    unsigned long start = millis();
    unsigned long lastSeconds = 0;
    unsigned long elapsed;
    String address = WiFi.localIP().toString();
    while (!failed && WiFi.status() == WL_CONNECTED && (elapsed = millis() - start) < OTA_WAIT_MS) {
        unsigned long seconds = (OTA_WAIT_MS - elapsed + 999) / 1000;
        if (seconds != lastSeconds) {
            char text[24];
            snprintf(text, sizeof(text), "Waiting: %lus", seconds);
            showOtaStatus("OTA update", text, address.c_str());
            lastSeconds = seconds;
        }
        ArduinoOTA.handle();
        mqttClient.loop();
        delay(10);
        ESP.wdtFeed();
    }
    // The caller disconnects WiFi and deep-sleeps; older ESP8266 cores have no OTA end().
    if (!failed) publishStatus(WiFi.status() == WL_CONNECTED ? "timed_out" : "wifi_lost");
    if (failed) delay(2000);
    clearDisplay();
}
}

void publishOtaDiscovery(const char* deviceJson) {
    char payload[640];
    snprintf(payload, sizeof(payload),
        "{\"name\":\"Prepare OTA update\",\"uniq_id\":\"env_sensor_ota\","
        "\"default_entity_id\":\"button.environmental_sensor_prepare_ota\","
        "\"cmd_t\":\"" MQTT_OTA_COMMAND_TOPIC "\",\"pl_prs\":\"START\","
        "\"ret\":true,\"qos\":1,\"ent_cat\":\"config\",\"ic\":\"mdi:upload-network\",%s}",
        deviceJson);
    mqttClient.publish("homeassistant/button/environmental_sensor/ota/config", payload, true);
    snprintf(payload, sizeof(payload),
        "{\"name\":\"OTA status\",\"uniq_id\":\"env_sensor_ota_status\","
        "\"default_entity_id\":\"sensor.environmental_sensor_ota_status\","
        "\"stat_t\":\"" MQTT_OTA_STATUS_TOPIC "\","
        "\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:update\",%s}", deviceJson);
    mqttClient.publish("homeassistant/sensor/environmental_sensor/ota_status/config", payload, true);
}

bool checkOtaRequest(float batteryVoltage) {
    requested = false;
    cleared = false;
    mqttClient.setCallback(receiveCommand);
    publishStatus("idle");
    if (!mqttClient.subscribe(MQTT_OTA_COMMAND_TOPIC, 1)) return false;
    waitForMessage(requested);
    if (!requested) {
        mqttClient.unsubscribe(MQTT_OTA_COMMAND_TOPIC);
        return false;
    }

    // Confirm broker receipt before allowing a reboot, avoiding repeated OTA windows.
    cleared = false;
    if (!mqttClient.publish(MQTT_OTA_COMMAND_TOPIC, "", true)) {
        mqttClient.unsubscribe(MQTT_OTA_COMMAND_TOPIC);
        publishStatus("request_failed");
        return false;
    }
    waitForMessage(cleared);
    mqttClient.unsubscribe(MQTT_OTA_COMMAND_TOPIC);
    if (!cleared) {
        publishStatus("request_failed");
        return false;
    }
    if (batteryVoltage < OTA_MIN_VOLTAGE) {
        publishStatus("low_battery");
        return false;
    }
    waitForUpload();
    return true;
}
