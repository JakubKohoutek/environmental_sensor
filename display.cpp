#include <U8g2lib.h>
#include <Wire.h>

#include "display.h"
#include "sensors.h"

// 1.3" 128x64 I2C OLED (SH1106 driver)
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

void initiateDisplay() {
    display.begin();
}

void clearDisplay() {
    display.begin();
    display.clearBuffer();
    display.sendBuffer();
    display.setPowerSave(1);
}

// Draw battery icon (16x8) with fill level
// Range: 3.5V (empty/LDO cutoff) to 4.1V (full)
void drawBatteryIcon(int x, int y, float voltage) {
    int level = (int)((voltage - 3.5) / 0.6 * 12);
    if (level < 0) level = 0;
    if (level > 12) level = 12;

    // Battery outline
    display.drawFrame(x, y, 16, 8);
    // Battery tip
    display.drawBox(x + 16, y + 2, 2, 4);
    // Fill level
    if (level > 0) {
        display.drawBox(x + 2, y + 2, level, 4);
    }
}

// Center a string horizontally within a region [xStart, xStart+width)
void drawCentered(int xStart, int width, int y, const char* str) {
    int sw = display.getStrWidth(str);
    display.drawStr(xStart + (width - sw) / 2, y, str);
}

void updateDisplay(float batteryVoltage) {
    display.clearBuffer();

    // Layout: two columns (0-63, 65-127), top half 0-41, bottom half 43-63
    // Vertical divider starts below headers (y=12) to bottom
    display.drawVLine(64, 12, 52);
    // Horizontal divider
    display.drawHLine(0, 42, 128);

    char val[16];

    // ── Temperature — top left (0-63) ──
    // Header
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(0, 64, 10, "Temp \xb0\x43");

    // Value
    display.setFont(u8g2_font_logisoso20_tr);
    if (sensorData.dhtOk) {
        snprintf(val, sizeof(val), "%.1f", sensorData.temperature);
    } else {
        snprintf(val, sizeof(val), "--.-");
    }
    drawCentered(0, 64, 36, val);

    // ── Humidity — top right (65-127) ──
    // Header
    display.setFont(u8g2_font_6x10_tf);
    drawCentered(65, 63, 10, "Hum %");

    // Value
    display.setFont(u8g2_font_logisoso20_tr);
    if (sensorData.dhtOk) {
        if (sensorData.humidity >= 99.95) {
            snprintf(val, sizeof(val), "100");
        } else {
            snprintf(val, sizeof(val), "%.1f", sensorData.humidity);
        }
    } else {
        snprintf(val, sizeof(val), "--.-");
    }
    drawCentered(65, 63, 36, val);

    // ── Pressure — bottom left (0-63) ──
    display.setFont(u8g2_font_7x13B_tf);
    if (sensorData.bmpOk) {
        snprintf(val, sizeof(val), "%s %.0f", "hPa", sensorData.pressure);
    } else {
        snprintf(val, sizeof(val), "hPa ----");
    }
    drawCentered(0, 64, 57, val);

    // ── Battery — bottom right (65-127) ──
    // Battery icon + voltage text, vertically centered in bottom row
    snprintf(val, sizeof(val), "%.2fV", batteryVoltage);
    display.setFont(u8g2_font_6x10_tf);
    int vw = display.getStrWidth(val);
    int groupWidth = 18 + 5 + vw; // icon(16+2 tip) + gap(5) + text
    int groupX = 65 + (63 - groupWidth) / 2;
    int centerY = 42 + (64 - 42) / 2;  // vertical center of bottom row

    drawBatteryIcon(groupX, centerY - 4, batteryVoltage);
    display.drawStr(groupX + 23, centerY + 4, val);

    display.sendBuffer();
}
