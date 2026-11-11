#ifndef TEST_PUBSUBCLIENT_H
#define TEST_PUBSUBCLIENT_H

#include "ESP8266WiFi.h"
#include <vector>

struct TestPublication {
    std::string topic;
    std::string payload;
    bool retained;
};

struct PubSubClient {
    bool isConnected = false;
    bool subscribed = false;
    bool pending = false;
    bool echoClear = true;
    bool allowClear = true;
    bool allowSubscribe = true;
    bool allowConnect = true;
    unsigned connections = 0;
    std::string retainedCommand;
    std::vector<TestPublication> publications;
    void (*callback)(char*, uint8_t*, unsigned int) = nullptr;
    explicit PubSubClient(WiFiClient&) {}
    bool setBufferSize(unsigned int) { return true; }
    void setCallback(void (*fn)(char*, uint8_t*, unsigned int)) { callback = fn; }
    void setServer(const char*, int) {}
    void setSocketTimeout(unsigned int) {}
    bool connected() { return isConnected; }
    bool connect(const char*, const char*, int, bool, const char*) {
        ++connections;
        isConnected = allowConnect;
        return isConnected;
    }
    bool publish(const char* topic, const char* payload, bool retained) {
        if (!isConnected) return false;
        publications.push_back({topic, payload, retained});
        if (strcmp(topic, "environmental_sensor/ota/set") == 0) {
            if (!allowClear) return false;
            retainedCommand = payload;
            pending = echoClear;
        }
        return true;
    }
    bool subscribe(const char*, int) {
        subscribed = allowSubscribe;
        pending = !retainedCommand.empty();
        return subscribed;
    }
    bool unsubscribe(const char*) { subscribed = false; return true; }
    void loop() {
        if (!isConnected || !subscribed || !pending || !callback) return;
        pending = false;
        char topic[] = "environmental_sensor/ota/set";
        std::string payload = retainedCommand;
        callback(topic, reinterpret_cast<uint8_t*>(&payload[0]), payload.size());
    }
    void disconnect() { isConnected = false; subscribed = false; }
};

#endif
