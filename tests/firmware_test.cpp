#include "stubs/U8g2lib.h"
#include "../environmental_sensor.ino"

TestEsp ESP;
TestWiFi WiFi;
uint32_t testMillis = 0;
int testPir = LOW;
int testAdc = 1000;
unsigned sensorReads = 0;
SensorData sensorData = {};
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C display;

void initiateSensors() {}
void readSensors(float) {
    ++sensorReads;
    delay(100);
    sensorData = {22, 50, 990, 1015, 235, 22, true, true};
}

static void reset() {
    ESP = {};
    WiFi = {};
    rtcState = {};
    sensorData = {};
    sensorReads = 0;
    testMillis = 0;
    testPir = LOW;
    testAdc = 1000;
}

static Sleep wake(int pir = LOW) {
    testPir = pir;
    testMillis = 0;
    // Globals are reinitialized by the real processor on every deep-sleep boot.
    rtcState = {};
    sensorData = {};
    try {
        setup();
    } catch (const Sleep& sleep) {
        assert(sleep.rfMode == WAKE_NO_RFCAL);
        return sleep;
    }
    assert(false);
    return {};
}

static void persistAt(uint32_t seconds) {
    rtcState.elapsedSeconds = seconds;
    rtcState.awakeRemainderMs = 0;
    ESP.rtcUserMemoryWrite(RTC_ADDR, reinterpret_cast<uint32_t*>(&rtcState), sizeof(rtcState));
}

static void assertLabel(int days, const char* expected) {
    updateDisplay(3.9f, days);
    bool found = false;
    for (const auto& text : display.text) {
        if (text.y != 57 || text.x >= 64) continue;
        assert(text.value == expected);
        assert(text.x >= 0 && text.x + text.width <= 64);
        found = true;
    }
    assert(found);
}

int main() {
    reset();
    assert(wake().seconds == 10);
    assert(WiFi.connections == 1 && sensorReads == 1);
    assert(display.powerSave);
    assert(rtcState.batteryLife.hasReading && !rtcState.batteryLife.hasFirstDrop);
    for (int i = 0; i < 29; ++i) assert(wake().seconds == 10);
    assert(sensorReads == 1 && WiFi.connections == 1);
    wake();
    assert(sensorReads == 2 && rtcState.skipCount == 1);
    assert(rtcState.lastFullCycleAt >= 300 && rtcState.lastFullCycleAt < 310);

    reset();
    wake();
    assert(wake(HIGH).seconds == 3);
    assert(rtcState.displayOn && display.contrast == 64 && !display.powerSave);
    assert(rtcState.lastDispDays == -1);
    uint32_t motionAt = rtcState.lastMotionAt;
    for (int i = 0; i < 9; ++i) assert(wake().seconds == 3);
    assert(rtcState.displayOn && WiFi.connections == 1);
    assert(wake().seconds == 10);
    assert(!rtcState.displayOn && display.powerSave);
    assert(rtcState.elapsedSeconds - 10 - motionAt == 30);

    reset();
    wake(HIGH);
    for (int i = 0; i < 99; ++i) assert(wake(HIGH).seconds == 3);
    assert(rtcState.displayOn && WiFi.connections == 1 && rtcState.skipCount == 0);
    wake(HIGH);
    assert(rtcState.skipCount == 1);
    assert(rtcState.lastFullCycleAt >= 300 && rtcState.lastFullCycleAt < 303);
    // Refreshes read sensors, but unchanged digits do not redraw the panel.
    unsigned frames = display.frames;
    for (int i = 0; i < 5; ++i) wake(HIGH);
    assert(display.frames == frames);

    reset();
    wake();
    for (int i = 0; i < 20; ++i) wake();
    for (int i = 0; i < 10; ++i) wake(HIGH);
    while (rtcState.elapsedSeconds < 300) wake();
    wake();
    assert(rtcState.lastFullCycleAt >= 300 && rtcState.lastFullCycleAt < 310);
    assert(rtcState.skipCount == 1 && WiFi.connections == 1);

    sensorData.temperature = rtcState.lastPubTemp;
    sensorData.humidity = rtcState.lastPubHum;
    rtcState.batteryVoltage = rtcState.lastPubBatt;
    sensorData.seaLevelPressure = rtcState.lastPubPres + 0.4f;
    assert(!shouldPublish());
    sensorData.seaLevelPressure = rtcState.lastPubPres + 0.5f;
    assert(shouldPublish());
    sensorData.seaLevelPressure = rtcState.lastPubPres - 0.5f;
    assert(shouldPublish());

    reset();
    wake();
    for (int cycle = 1; cycle <= 6; ++cycle) {
        persistAt(cycle * 300);
        wake();
        assert(WiFi.connections == (cycle == 6 ? 2u : 1u));
    }

    reset();
    wake(HIGH);
    testAdc = 850;
    unsigned reads = sensorReads;
    assert(wake().seconds == 10);
    assert(!rtcState.displayOn && display.powerSave);
    assert(sensorReads == reads && WiFi.connections == 1);
    assert(wake(HIGH).seconds == 3);
    assert(rtcState.lowBatteryWarningShown && !display.powerSave && display.contrast == 64);
    assert(wake(HIGH).seconds == 3);
    assert(!rtcState.lowBatteryWarningShown && display.powerSave);
    wake(HIGH);
    testAdc = 1000;
    wake();
    assert(!rtcState.lowBatteryWarningShown && display.powerSave);

    reset();
    wake();
    rtcState.batteryLife = {};
    for (uint32_t time = 0; time <= 48 * 3600; time += 300) {
        recordBatteryVoltage(rtcState.batteryLife, time, 4.1f - 0.02f * time / 86400.0f);
    }
    testAdc = 1013;
    persistAt(48 * 3600);
    wake(HIGH);
    int estimatedDays = rtcState.lastDispDays;
    assert(estimatedDays >= 26 && estimatedDays <= 30);
    assert(rtcState.batteryLife.hasFirstDrop && rtcState.batteryLife.firstDropAt > 300);
    uint32_t firstDropAt = rtcState.batteryLife.firstDropAt;
    wake(HIGH);
    assert(rtcState.batteryLife.firstDropAt == firstDropAt);
    frames = display.frames;
    rtcState.lastDispDays = estimatedDays + 1;
    persistAt(rtcState.lastDisplayRefreshAt + DISPLAY_REFRESH_SECONDS);
    wake(HIGH);
    assert(rtcState.lastDispDays == estimatedDays && display.frames == frames + 1);
    rtcState.magic = 0xE5A70008;
    persistAt(48 * 3600);
    wake(HIGH);
    assert(rtcState.magic == RTC_MAGIC && !rtcState.batteryLife.hasFirstDrop);
    assert(rtcState.lastDispDays == -1);

    assertLabel(-1, "-- days");
    assertLabel(0, "<1 day");
    assertLabel(1, "~1 day");
    assertLabel(28, "~28 days");
    assertLabel(99, "~99 days");
    assertLabel(100, "~100d");

    // Both scheduler arithmetic and the battery baseline remain valid across clock wrap.
    reset();
    wake();
    rtcState.lastFullCycleAt = UINT32_MAX - 100;
    persistAt(199);
    wake();
    assert(rtcState.skipCount == 1);

    rtcState.elapsedSeconds = 7;
    rtcState.awakeRemainderMs = 800;
    testMillis = 1750;
    try {
        saveAndSleep(10);
    } catch (const Sleep& sleep) {
        assert(sleep.seconds == 10 && sleep.rfMode == WAKE_NO_RFCAL);
    }
    assert(rtcState.elapsedSeconds == 19 && rtcState.awakeRemainderMs == 550);

    printf("Firmware tests passed (RTC state: %zu/512 bytes)\n", sizeof(RtcState));
}
