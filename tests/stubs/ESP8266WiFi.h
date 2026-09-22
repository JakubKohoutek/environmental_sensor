#ifndef TEST_ESP8266_WIFI_H
#define TEST_ESP8266_WIFI_H

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

constexpr int D6 = 12, D4 = 2, A0 = 17;
constexpr int HIGH = 1, LOW = 0, INPUT = 0, OUTPUT = 1;
constexpr int WIFI_STA = 1, WIFI_OFF = 0, WL_CONNECTED = 3;
constexpr int WDTO_8S = 8, WAKE_NO_RFCAL = 2, HEX = 16;

class String : public std::string {
public:
    String(const std::string& value) : std::string(value) {}
    String(uint32_t value, int) : std::string(std::to_string(value)) {}
};

extern uint32_t testMillis;
extern int testPir;
extern int testAdc;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t ms) { testMillis += ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return testPir; }
inline int analogRead(int) { return testAdc; }
template <size_t N>
inline char* dtostrf(double value, int width, int precision, char (&out)[N]) {
    snprintf(out, N, "%*.*f", width, precision, value);
    return out;
}

struct Sleep {
    uint32_t seconds;
    int rfMode;
};

struct TestEsp {
    uint8_t memory[512] = {};
    void wdtEnable(int) {}
    void wdtFeed() {}
    uint32_t getChipId() { return 1; }
    bool rtcUserMemoryRead(uint32_t, uint32_t* dest, size_t size) {
        assert(size <= sizeof(memory));
        memcpy(dest, memory, size);
        return true;
    }
    bool rtcUserMemoryWrite(uint32_t, uint32_t* src, size_t size) {
        assert(size <= sizeof(memory));
        memcpy(memory, src, size);
        return true;
    }
    [[noreturn]] void deepSleep(uint32_t us, int rfMode) {
        throw Sleep{us / 1000000, rfMode};
    }
};
extern TestEsp ESP;

struct TestWiFi {
    bool connected = false;
    unsigned connections = 0;
    uint8_t bssid[6] = {};
    void mode(int mode) { if (mode == WIFI_OFF) connected = false; }
    void setOutputPower(float) {}
    void begin(const char*, const char*) { connected = true; ++connections; }
    void begin(const char* ssid, const char* password, int, const uint8_t*, bool) {
        begin(ssid, password);
    }
    int status() { return connected ? WL_CONNECTED : 0; }
    uint8_t* BSSID() { return bssid; }
    int channel() { return 1; }
    void disconnect(bool) { connected = false; }
};
extern TestWiFi WiFi;
struct WiFiClient {};

#endif
