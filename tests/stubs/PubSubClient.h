#ifndef TEST_PUBSUBCLIENT_H
#define TEST_PUBSUBCLIENT_H

#include "ESP8266WiFi.h"

struct PubSubClient {
    bool isConnected = false;
    explicit PubSubClient(WiFiClient&) {}
    void setServer(const char*, int) {}
    bool connected() { return isConnected; }
    bool connect(const char*, const char*, int, bool, const char*) {
        isConnected = true;
        return true;
    }
    bool publish(const char*, const char*, bool) { return true; }
    void loop() {}
    void disconnect() { isConnected = false; }
};

#endif
