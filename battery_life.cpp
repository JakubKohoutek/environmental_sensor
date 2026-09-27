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
        state.initialVoltage = voltage;
        state.lowestVoltage = voltage;
        state.hasReading = 1;
    } else {
        if (!state.hasFirstDrop && voltage < state.initialVoltage) {
            state.firstDropAt = now;
            state.firstDropVoltage = voltage;
            state.hasFirstDrop = 1;
        }
        if (voltage < state.lowestVoltage) state.lowestVoltage = voltage;
    }
    state.lastReadingAt = now;
    return reset;
}

int estimateBatteryDays(const BatteryLifeState& state, uint32_t now,
                        float voltage, float cutoff) {
    if (voltage <= cutoff) return 0;
    if (!state.hasFirstDrop || state.rechargeReadings ||
        now == state.firstDropAt || voltage >= state.firstDropVoltage) return -1;

    double elapsedHours = (now - state.firstDropAt) / 3600.0;
    double dropPerHour = (state.firstDropVoltage - voltage) / elapsedHours;
    double days = (voltage - cutoff) / dropPerHour / 24.0;
    if (days >= INT_MAX) return INT_MAX;
    return static_cast<int>(round(days));
}
