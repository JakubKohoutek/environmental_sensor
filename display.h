#ifndef DISPLAY_H
#define DISPLAY_H

void initiateDisplay();
/** Returns a rounded percentage, clamped from 0% at 3.6 V to 100% at 4.05 V. */
int batteryPercentage(float voltage);
/** Renders sensor readings, voltage, and a battery icon with its percentage. */
void updateDisplay(float batteryVoltage);
void showLowBatteryWarning(float batteryVoltage);
/** Draws OTA waiting, progress, or result information without reinitializing the panel. */
void showOtaStatus(const char* title, const char* detail, const char* footer);
void clearDisplay();

#endif // DISPLAY_H
