#ifndef BATTERY_LIFE_H
#define BATTERY_LIFE_H

#include <stdint.h>

constexpr uint32_t BATTERY_READING_SECONDS = 300;
constexpr uint32_t BATTERY_AVERAGE_SAMPLES = 6;
constexpr float BATTERY_FIRST_DROP_VOLTS = 0.012f;
constexpr float BATTERY_ESTIMATE_DROP_VOLTS = 0.02f;

struct BatteryLifeState {
    float readings[BATTERY_AVERAGE_SAMPLES];
    uint32_t readingCount;
    uint32_t nextReading;
    float smoothedVoltage;
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

// -1 means no meaningful first drop, insufficient decline, or suspected recharge.
// The voltage argument is the latest raw reading, used for an immediate cutoff check.
int estimateBatteryDays(const BatteryLifeState& state, uint32_t now,
                        float voltage, float cutoff);

#endif
