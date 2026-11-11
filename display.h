#ifndef DISPLAY_H
#define DISPLAY_H

/** Restores the interface each boot; initializes the panel only without valid RTC state. */
void prepareDisplay(bool retainedState);
/** Enables the dimmed panel without resetting it or clearing its image. */
void initiateDisplay();
/** Returns a rounded percentage, clamped from 0% at 3.6 V to 100% at 4.05 V. */
int batteryPercentage(float voltage);
/** Renders sensor readings, voltage, and a battery icon with its percentage. */
void updateDisplay(float batteryVoltage);
void showLowBatteryWarning(float batteryVoltage);
/** Draws OTA waiting, progress, or result information without reinitializing the panel. */
void showOtaStatus(const char* title, const char* detail, const char* footer);
/** Powers down and clears the panel without briefly switching it on. */
void clearDisplay();

#endif // DISPLAY_H
