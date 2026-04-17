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

// Draw a small trend arrow (5x5) at position
void drawTrend(int x, int y, Trend trend) {
    if (trend == TREND_UP) {
        // Up triangle
        display.drawTriangle(x + 2, y, x, y + 4, x + 4, y + 4);
    } else if (trend == TREND_DOWN) {
        // Down triangle
        display.drawTriangle(x, y, x + 4, y, x + 2, y + 4);
    } else {
        // Stable: horizontal line with right arrow
        display.drawHLine(x, y + 2, 4);
        display.drawTriangle(x + 3, y, x + 3, y + 4, x + 5, y + 2);
    }
}

// Center a string horizontally within a region [xStart, xStart+width)
void drawCentered(int xStart, int width, int y, const char* str) {
    int sw = display.getStrWidth(str);
    display.drawStr(xStart + (width - sw) / 2, y, str);
}

void updateDisplay(float batteryVoltage, const char* forecast, Trend tempTrend, Trend humTrend, Trend presTrend) {
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
    int tw = display.getStrWidth(val);
    int tx = (64 - tw) / 2 - 3; // Shift left slightly to make room for arrow
    display.drawStr(tx, 36, val);
    // Trend arrow to the right of value
    if (sensorData.dhtOk) {
        drawTrend(tx + tw + 2, 24, tempTrend);
    }

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
    int hw = display.getStrWidth(val);
    int hx = 65 + (63 - hw) / 2 - 3;
    display.drawStr(hx, 36, val);
    if (sensorData.dhtOk) {
        drawTrend(hx + hw + 2, 24, humTrend);
    }

    // ── Pressure + forecast — bottom left (0-63) ──
    display.setFont(u8g2_font_6x10_tf);
    if (sensorData.bmpOk) {
        snprintf(val, sizeof(val), "%.0f hPa", sensorData.seaLevelPressure);
    } else {
        snprintf(val, sizeof(val), "---- hPa");
    }
    int pw = display.getStrWidth(val);
    int px = (64 - pw) / 2 - 3;
    display.drawStr(px, 52, val);
    if (sensorData.bmpOk) {
        drawTrend(px + pw + 2, 45, presTrend);
    }
    // Forecast text below pressure
    if (forecast[0] != '\0') {
        drawCentered(0, 64, 63, forecast);
    }

    // ── Battery — bottom right (65-127) ──
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
