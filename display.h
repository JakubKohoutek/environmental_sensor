#ifndef DISPLAY_H
#define DISPLAY_H

void initiateDisplay();
void updateDisplay(float batteryVoltage, const char* forecast = "");
void clearDisplay();

#endif // DISPLAY_H
