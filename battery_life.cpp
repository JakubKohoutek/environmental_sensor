#include "battery_life.h"

#include <math.h>
#include <limits.h>

bool recordBatteryVoltage(BatteryLifeState& state, uint32_t now, float voltage) {
    if (state.hasReading &&
        now - state.lastReadingAt < BATTERY_READING_SECONDS) {
        return false;
    }

    bool reset = false;
    if (state.hasReading) {
        state.rechargeReadings = voltage >= state.lowestVoltage + 0.15f
            ? state.rechargeReadings + 1 : 0;
        if (state.rechargeReadings >= 3) {
            state = {};
            reset = true;
        }
    }

    if (!state.hasReading) {
        state.lowestVoltage = voltage;
        state.hasReading = 1;
    } else {
        if (voltage < state.lowestVoltage) state.lowestVoltage = voltage;
    }
    state.lastReadingAt = now;
    state.readings[state.nextReading] = voltage;
    state.nextReading = (state.nextReading + 1) % BATTERY_AVERAGE_SAMPLES;
    bool firstWindow = state.readingCount == BATTERY_AVERAGE_SAMPLES - 1;
    if (state.readingCount < BATTERY_AVERAGE_SAMPLES) ++state.readingCount;
    if (state.readingCount == BATTERY_AVERAGE_SAMPLES) {
        float total = 0;
        for (uint32_t i = 0; i < BATTERY_AVERAGE_SAMPLES; ++i) {
            total += state.readings[i];
        }
        state.smoothedVoltage = total / BATTERY_AVERAGE_SAMPLES;
        if (firstWindow) state.initialVoltage = state.smoothedVoltage;
        if (!state.hasFirstDrop &&
            state.initialVoltage - state.smoothedVoltage >= BATTERY_FIRST_DROP_VOLTS) {
            state.firstDropAt = now;
            state.firstDropVoltage = state.smoothedVoltage;
            state.hasFirstDrop = 1;
        }
    }
    return reset;
}

int estimateBatteryDays(const BatteryLifeState& state, uint32_t now,
                        float voltage, float cutoff) {
    if (voltage <= cutoff) return 0;
    if (!state.hasFirstDrop || state.rechargeReadings ||
        now == state.firstDropAt ||
        state.firstDropVoltage - state.smoothedVoltage < BATTERY_ESTIMATE_DROP_VOLTS) return -1;

    double elapsedHours = (now - state.firstDropAt) / 3600.0;
    double dropPerHour = (state.firstDropVoltage - state.smoothedVoltage) / elapsedHours;
    double days = (state.smoothedVoltage - cutoff) / dropPerHour / 24.0;
    if (days <= 0) return 0;
    if (days >= INT_MAX) return INT_MAX;
    return static_cast<int>(round(days));
}
