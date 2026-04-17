#ifndef DISPLAY_H
#define DISPLAY_H

// Trend direction for display arrows
enum Trend { TREND_STABLE, TREND_UP, TREND_DOWN };

void initiateDisplay();
void updateDisplay(float batteryVoltage, const char* forecast = "", Trend tempTrend = TREND_STABLE, Trend humTrend = TREND_STABLE, Trend presTrend = TREND_STABLE);
void clearDisplay();

#endif // DISPLAY_H
