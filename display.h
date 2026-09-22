#ifndef DISPLAY_H
#define DISPLAY_H

void initiateDisplay();
void updateDisplay(float batteryVoltage, int batteryDays = -1);
void showLowBatteryWarning(float batteryVoltage);
void clearDisplay();

#endif // DISPLAY_H
