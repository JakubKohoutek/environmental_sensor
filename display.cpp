#include <U8g2lib.h>
#include <Wire.h>

#include "display.h"
#include "sensors.h"

// 1.3" 128x64 I2C OLED (SH1106 driver)
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

void initiateDisplay() {
    display.begin();
    display.setContrast(64);
}

void clearDisplay() {
    initiateDisplay();
    display.clearBuffer();
    display.sendBuffer();
    display.setPowerSave(1);
}

int batteryPercentage(float voltage) {
    if (voltage <= 3.6f) return 0;
    if (voltage >= 4.05f) return 100;
    return static_cast<int>((voltage - 3.6f) / (4.05f - 3.6f) * 100 + 0.5f);
}

/** Draws a 16x8 battery body with a 12-pixel fill based on a 0-100 percentage. */
void drawBatteryIcon(int x, int y, int percentage) {
    int level = percentage * 12 / 100;

    display.drawFrame(x, y, 16, 8);
    display.drawBox(x + 16, y + 2, 2, 4);
    if (level > 0) {
        display.drawBox(x + 2, y + 2, level, 4);
    }
}

// Center a string horizontally within a region [xStart, xStart+width)
void drawCentered(int xStart, int width, int y, const char* str) {
    int sw = display.getStrWidth(str);
    display.drawStr(xStart + (width - sw) / 2, y, str);
}

// Full-screen low battery warning: large crossed-out battery icon + text + voltage.
void showLowBatteryWarning(float batteryVoltage) {
    display.clearBuffer();

    // Large battery body — 70x26 horizontally centered (including the 4px
    // terminal, so overall icon width is 74 → x = (128 - 74) / 2 = 27).
    int bx = 27;
    int by = 4;
    int bw = 70;
    int bh = 26;
    display.drawFrame(bx,       by,     bw,     bh);
    display.drawFrame(bx + 1,   by + 1, bw - 2, bh - 2);
    // Battery terminal (positive cap on the right)
    display.drawBox(bx + bw, by + 8, 4, bh - 16);

    // Diagonal "crossed-out" lines across the battery
    display.drawLine(bx,      by,      bx + bw, by + bh);
    display.drawLine(bx,      by + bh, bx + bw, by);
    display.drawLine(bx + 1,  by,      bx + bw, by + bh - 1);
    display.drawLine(bx,      by + bh - 1, bx + bw - 1, by);

    // Warning text
    display.setFont(u8g2_font_7x13B_tf);
    drawCentered(0, 128, 46, "Low battery!");

    // Voltage
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f V", batteryVoltage);
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(0, 128, 60, buf);

    display.sendBuffer();
}

void showOtaStatus(const char* title, const char* detail, const char* footer) {
    display.clearBuffer();
    display.setFont(u8g2_font_7x13B_tf);
    drawCentered(0, 128, 16, title);
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(0, 128, 36, detail);
    drawCentered(0, 128, 56, footer);
    display.sendBuffer();
}

void updateDisplay(float batteryVoltage) {
    display.clearBuffer();

    // Sensor readings above, voltage and battery percentage below.
    display.drawHLine(0, 42, 128);

    char val[16];

    // ── Temperature — top left (0-63) ──
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(0, 64, 10, "Temp \xb0\x43");

    display.setFont(u8g2_font_logisoso20_tr);
    if (sensorData.ahtOk) {
        snprintf(val, sizeof(val), "%.1f", sensorData.temperature);
    } else {
        snprintf(val, sizeof(val), "--.-");
    }
    drawCentered(0, 64, 36, val);

    // ── Humidity — top right (65-127) ──
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(65, 63, 10, "Hum %");

    display.setFont(u8g2_font_logisoso20_tr);
    if (sensorData.ahtOk) {
        if (sensorData.humidity >= 99.95) {
            snprintf(val, sizeof(val), "100");
        } else {
            snprintf(val, sizeof(val), "%.1f", sensorData.humidity);
        }
    } else {
        snprintf(val, sizeof(val), "--.-");
    }
    drawCentered(65, 63, 36, val);

    // Battery voltage, bottom left.
    display.setFont(u8g2_font_6x10_tf);
    snprintf(val, sizeof(val), "%.2fV", batteryVoltage);
    drawCentered(0, 64, 57, val);

    // Battery icon and percentage, bottom right.
    int percentage = batteryPercentage(batteryVoltage);
    snprintf(val, sizeof(val), "%d%%", percentage);
    int vw = display.getStrWidth(val);
    int groupWidth = 18 + 5 + vw;
    int groupX = 65 + (63 - groupWidth) / 2;
    int centerY = 42 + (64 - 42) / 2;

    drawBatteryIcon(groupX, centerY - 4, percentage);
    display.drawStr(groupX + 23, centerY + 4, val);

    display.sendBuffer();
}
