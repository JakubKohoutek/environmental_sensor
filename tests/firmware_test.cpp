#include "stubs/U8g2lib.h"
#include "stubs/ArduinoOTA.h"
#include "../environmental_sensor.ino"

TestEsp ESP;
TestWiFi WiFi;
TestArduinoOTA ArduinoOTA;
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
    ArduinoOTA = {};
    mqttClient = PubSubClient(wifiClient);
    display = U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
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
    // The MCU's interface state is lost, but the powered OLED retains its frame.
    display.interfaceReady = false;
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

/** Checks the percentage label, adjacent icon fill, and divider-free layout. */
static void assertBatteryDisplay(float voltage, int percentage, int fillWidth) {
    assert(batteryPercentage(voltage) == percentage);
    updateDisplay(voltage);
    char expected[16];
    snprintf(expected, sizeof(expected), "%d%%", percentage);
    assert(display.verticalLines == 0);
    assert(display.outlines.size() == 1);
    const auto& icon = display.outlines[0];
    assert(icon.width == 16 && icon.height == 8);
    assert(display.boxes.size() == (fillWidth ? 2u : 1u));
    if (fillWidth) {
        const auto& fill = display.boxes[1];
        assert(fill.x == icon.x + 2 && fill.y == icon.y + 2);
        assert(fill.width == fillWidth && fill.height == 4);
    }
    bool found = false;
    bool voltageFound = false;
    char voltageLabel[16];
    snprintf(voltageLabel, sizeof(voltageLabel), "%.2fV", voltage);
    for (const auto& text : display.text) {
        assert(text.value.find("day") == std::string::npos);
        if (text.y != 57) continue;
        assert(text.x >= 0 && text.x + text.width <= 128);
        if (text.x >= 64) {
            assert(text.value == expected && text.x == icon.x + 23);
            found = true;
        } else {
            assert(text.value == voltageLabel && text.x + text.width <= 64);
            voltageFound = true;
        }
    }
    assert(found && voltageFound);
}

static bool hasOtaStatus(const char* status) {
    for (const auto& message : mqttClient.publications) {
        if (message.topic == MQTT_OTA_STATUS_TOPIC && message.payload == status && message.retained) return true;
    }
    return false;
}

static bool hasText(const char* expected) {
    for (const auto& text : display.text) {
        assert(text.x >= 0 && text.x + text.width <= 128);
        if (text.value == expected) return true;
    }
    return false;
}

int main() {
    reset();
    assert(wake().seconds == 10);
    assert(WiFi.connections == 1 && sensorReads == 1);
    assert(display.powerSave);
    for (int i = 0; i < 29; ++i) assert(wake().seconds == 10);
    assert(sensorReads == 1 && WiFi.connections == 1);
    wake();
    assert(sensorReads == 2 && rtcState.skipCount == 1);
    assert(rtcState.lastFullCycleAt >= 300 && rtcState.lastFullCycleAt < 310);

    reset();
    wake(HIGH);
    assert(display.panelInitializations == 1 && !display.powerSave);
    unsigned panelInitializations = display.panelInitializations;
    unsigned interfaceInitializations = display.interfaceInitializations;
    unsigned powerOffCommands = display.powerOffCommands;
    unsigned retainedFrames = display.frames;
    wake(HIGH);
    assert(display.interfaceInitializations == interfaceInitializations + 1);
    assert(display.frames == retainedFrames && !display.powerSave);

    // Force changed visible data after a deep-sleep boot, not just an unchanged frame.
    rtcState.lastDispTemp -= 1;
    persistAt(rtcState.lastDisplayRefreshAt + DISPLAY_REFRESH_SECONDS);
    wake(HIGH);
    assert(display.frames == retainedFrames + 1 && !display.powerSave);
    assert(display.panelInitializations == panelInitializations);
    assert(display.powerOffCommands == powerOffCommands);

    persistAt(rtcState.lastMotionAt + DISPLAY_ON_SECONDS);
    wake();
    assert(display.powerSave && display.powerOffCommands == powerOffCommands + 1);
    wake(HIGH);
    assert(!display.powerSave && display.contrast == 64);
    assert(display.panelInitializations == panelInitializations);
    assert(display.powerOffCommands == powerOffCommands + 1);

    rtcState.magic = 0;
    persistAt(rtcState.elapsedSeconds);
    wake(HIGH);
    assert(display.panelInitializations == panelInitializations + 1);
    assert(!display.powerSave);

    reset();
    wake();
    assert(wake(HIGH).seconds == 3);
    assert(rtcState.displayOn && display.contrast == 64 && !display.powerSave);
    uint32_t motionAt = rtcState.lastMotionAt;
    for (int i = 0; i < 9; ++i) assert(wake().seconds == 3);
    assert(rtcState.displayOn && WiFi.connections == 1);
    assert(wake().seconds == 10);
    assert(!rtcState.displayOn && display.powerSave);
    assert(rtcState.elapsedSeconds - 10 - motionAt == 30);

    reset();
    wake(HIGH);
    while (rtcState.elapsedSeconds < 300) assert(wake(HIGH).seconds == 3);
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
    assert(display.panelInitializations == 1);

    reset();
    testAdc = 950;
    wake(HIGH);
    assert(batteryPercentage(rtcState.lastDispBatt) == 46);
    frames = display.frames;
    // A one-ADC-step change is below the voltage redraw threshold but changes %.
    testAdc = 951;
    assert(VBAT_MULTIPLIER < DISP_BATT_THRESHOLD);
    persistAt(rtcState.lastDisplayRefreshAt + DISPLAY_REFRESH_SECONDS);
    wake(HIGH);
    assert(batteryPercentage(rtcState.lastDispBatt) == 47 && display.frames == frames + 1);
    frames = display.frames;
    persistAt(rtcState.lastDisplayRefreshAt + DISPLAY_REFRESH_SECONDS);
    wake(HIGH);
    assert(display.frames == frames);
    rtcState.magic = 0xE5A7000B;
    persistAt(48 * 3600);
    wake(HIGH);
    assert(rtcState.magic == RTC_MAGIC && rtcState.elapsedSeconds < 10);

    reset();
    testAdc = 896; // About 3.59 V: empty display, but above the 3.5 V cutoff.
    wake(HIGH);
    assert(rtcState.displayOn && !rtcState.lowBatteryWarningShown);
    assert(batteryPercentage(rtcState.lastDispBatt) == 0);

    assertBatteryDisplay(3.5f, 0, 0);
    assertBatteryDisplay(3.59f, 0, 0);
    assertBatteryDisplay(3.6f, 0, 0);
    assertBatteryDisplay(3.645f, 10, 1);
    assertBatteryDisplay(3.825f, 50, 6);
    assertBatteryDisplay(3.87f, 60, 7);
    assertBatteryDisplay(4.005f, 90, 10);
    assertBatteryDisplay(4.05f, 100, 12);
    assertBatteryDisplay(4.2f, 100, 12);

    // Scheduler arithmetic remains valid across clock wrap.
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

    reset();
    wake();
    assert(!ArduinoOTA.starts && hasOtaStatus("idle"));
    bool discoveredButton = false;
    bool discoveredStatus = false;
    for (const auto& message : mqttClient.publications) {
        if (message.topic == "homeassistant/button/environmental_sensor/ota/config") {
            assert(message.payload.find("\"ret\":true") != std::string::npos);
            assert(message.payload.find(MQTT_OTA_COMMAND_TOPIC) != std::string::npos);
            assert(message.payload.find("\"pl_prs\":\"START\"") != std::string::npos);
            discoveredButton = message.retained;
        }
        if (message.topic == "homeassistant/sensor/environmental_sensor/ota_status/config") {
            discoveredStatus = message.retained;
        }
    }
    assert(discoveredButton && discoveredStatus);

    reset();
    mqttClient.retainedCommand = "START";
    ArduinoOTA.duringHandle = []() {
        assert(!display.powerSave && display.contrast == 64);
        assert(display.panelInitializations == 1);
        assert(hasText("OTA update") && hasText("192.168.68.123"));
        if (ArduinoOTA.handles == 1) assert(hasText("Waiting: 300s"));
    };
    wake(HIGH);
    assert(ArduinoOTA.starts == 1);
    assert(mqttClient.retainedCommand.empty() && hasOtaStatus("waiting") && hasOtaStatus("timed_out"));
    assert(!rtcState.displayOn && display.powerSave && !WiFi.connected);
    assert(rtcState.elapsedSeconds >= 300 && rtcState.elapsedSeconds < 310);
    wake(HIGH);
    assert(ArduinoOTA.starts == 1 && rtcState.displayOn && !display.powerSave);

    reset();
    mqttClient.retainedCommand = "START";
    mqttClient.echoClear = false;
    wake();
    assert(!ArduinoOTA.starts && hasOtaStatus("request_failed"));

    reset();
    mqttClient.retainedCommand = "START";
    mqttClient.allowClear = false;
    wake();
    assert(!ArduinoOTA.starts && mqttClient.retainedCommand == "START" && hasOtaStatus("request_failed"));

    reset();
    mqttClient.retainedCommand = "START";
    testAdc = 896;
    wake();
    assert(!ArduinoOTA.starts && hasOtaStatus("low_battery") && mqttClient.retainedCommand.empty());

    reset();
    mqttClient.retainedCommand = "STARTextra";
    wake();
    assert(!ArduinoOTA.starts);

    reset();
    mqttClient.retainedCommand = "START";
    ArduinoOTA.duringHandle = []() { ArduinoOTA.onErrorFn(1); };
    wake(HIGH);
    assert(ArduinoOTA.starts == 1 && hasOtaStatus("error"));
    assert(!rtcState.displayOn && display.powerSave);

    reset();
    mqttClient.retainedCommand = "START";
    ArduinoOTA.duringHandle = []() { WiFi.connected = false; };
    wake();
    assert(display.powerSave && !WiFi.connected && rtcState.elapsedSeconds < 20);

    reset();
    mqttClient.retainedCommand = "START";
    ArduinoOTA.duringHandle = []() {
        // Match the installed ArduinoOTA library's stopAll() before onStart.
        mqttClient.disconnect();
        ArduinoOTA.onStartFn();
        assert(hasText("Updating firmware") && hasOtaStatus("updating"));
        assert(mqttClient.connections == 2);
        // An upload already in progress may finish beyond the waiting deadline.
        delay(OTA_WAIT_MS + 1000);
        ArduinoOTA.onProgressFn(50, 100);
        assert(hasText("50%") && !display.powerSave);
        ArduinoOTA.onEndFn();
        assert(hasText("OTA complete") && hasOtaStatus("complete"));
        assert(!mqttClient.connected());
        throw Restart{};
    };
    try {
        wake();
        assert(false);
    } catch (const Restart&) {
        assert(mqttClient.retainedCommand.empty());
    }

    reset();
    mqttClient.retainedCommand = "START";
    ArduinoOTA.duringHandle = []() {
        mqttClient.disconnect();
        mqttClient.allowConnect = false;
        ArduinoOTA.onStartFn();
        assert(hasText("Updating firmware"));
        ArduinoOTA.onProgressFn(100, 100);
        assert(hasText("100%"));
        ArduinoOTA.onEndFn();
        throw Restart{};
    };
    try {
        wake();
        assert(false);
    } catch (const Restart&) {
        assert(mqttClient.retainedCommand.empty());
    }

    printf("Firmware tests passed (RTC state: %zu/512 bytes)\n", sizeof(RtcState));
}
