#ifndef BATTERY_LIFE_H
#define BATTERY_LIFE_H

#include <stdint.h>

constexpr uint32_t BATTERY_READING_SECONDS = 300;
constexpr uint32_t BATTERY_BUCKET_SECONDS = 6 * 60 * 60;
constexpr uint32_t BATTERY_HISTORY_SIZE = 29;

struct BatterySample {
    uint32_t seconds;
    float voltage;
};

struct BatteryLifeState {
    BatterySample history[BATTERY_HISTORY_SIZE];
    uint32_t count;
    uint32_t next;
    uint32_t bucketStartedAt;
    uint32_t lastReadingAt;
    uint32_t readingCount;
    uint32_t offsetSum;
    float voltageSum;
    uint32_t rechargeReadings;
};

// Returns true when a sustained voltage rise resets the discharge history.
bool recordBatteryVoltage(BatteryLifeState& state, uint32_t now, float voltage);

// -1 means learning, recharging, or no reliable downward trend.
int estimateBatteryDays(const BatteryLifeState& state, uint32_t now,
                        float voltage, float cutoff);

#endif
