#ifndef TEST_U8G2LIB_H
#define TEST_U8G2LIB_H

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

constexpr int U8G2_R0 = 0, U8X8_PIN_NONE = 0;
constexpr int u8g2_font_7x13B_tf = 7, u8g2_font_6x10_tf = 6;
constexpr int u8g2_font_logisoso20_tr = 10;

struct TestText {
    int x;
    int y;
    int width;
    std::string value;
};

struct TestRect {
    int x;
    int y;
    int width;
    int height;
};

struct U8G2_SH1106_128X64_NONAME_F_HW_I2C {
    int contrast = 255;
    bool powerSave = false;
    int fontWidth = 6;
    unsigned frames = 0;
    std::vector<TestText> text;
    std::vector<TestRect> boxes;
    std::vector<TestRect> outlines;
    unsigned verticalLines = 0;
    U8G2_SH1106_128X64_NONAME_F_HW_I2C(int, int) {}
    void begin() { powerSave = false; contrast = 255; }
    void setContrast(int value) { contrast = value; }
    void setPowerSave(int value) { powerSave = value; }
    void clearBuffer() { text.clear(); boxes.clear(); outlines.clear(); verticalLines = 0; }
    void sendBuffer() { ++frames; }
    void setFont(int font) { fontWidth = font; }
    int getStrWidth(const char* value) { return strlen(value) * fontWidth; }
    void drawStr(int x, int y, const char* value) {
        text.push_back({x, y, getStrWidth(value), value});
    }
    void drawFrame(int x, int y, int width, int height) {
        outlines.push_back({x, y, width, height});
    }
    void drawBox(int x, int y, int width, int height) {
        boxes.push_back({x, y, width, height});
    }
    void drawLine(int, int, int, int) {}
    void drawVLine(int, int, int) { ++verticalLines; }
    void drawHLine(int, int, int) {}
};

#endif
