#ifndef TEST_ARDUINO_OTA_H
#define TEST_ARDUINO_OTA_H

#include "ESP8266WiFi.h"
#include <functional>

using ota_error_t = int;
struct Restart {};

struct TestArduinoOTA {
    unsigned starts = 0;
    unsigned handles = 0;
    std::function<void()> onStartFn;
    std::function<void()> onEndFn;
    std::function<void(ota_error_t)> onErrorFn;
    std::function<void(unsigned int, unsigned int)> onProgressFn;
    std::function<void()> duringHandle;
    void setHostname(const char*) {}
    void setPassword(const char*) {}
    void onStart(std::function<void()> fn) { onStartFn = fn; }
    void onEnd(std::function<void()> fn) { onEndFn = fn; }
    void onError(std::function<void(ota_error_t)> fn) { onErrorFn = fn; }
    void onProgress(std::function<void(unsigned int, unsigned int)> fn) { onProgressFn = fn; }
    void begin() { ++starts; }
    void handle() { ++handles; if (duringHandle) duringHandle(); }
};

extern TestArduinoOTA ArduinoOTA;

#endif
