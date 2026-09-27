#ifndef BATTERY_LIFE_H
#define BATTERY_LIFE_H

#include <stdint.h>

constexpr uint32_t BATTERY_READING_SECONDS = 300;

struct BatteryLifeState {
    uint32_t lastReadingAt;
    float initialVoltage;
    uint32_t firstDropAt;
    float firstDropVoltage;
    float lowestVoltage;
    uint32_t rechargeReadings;
    uint32_t hasReading;
    uint32_t hasFirstDrop;
};

// Returns true when a sustained voltage rise resets the discharge baseline.
bool recordBatteryVoltage(BatteryLifeState& state, uint32_t now, float voltage);

// -1 means waiting for a drop after the first drop, or recharging.
int estimateBatteryDays(const BatteryLifeState& state, uint32_t now,
                        float voltage, float cutoff);

#endif
