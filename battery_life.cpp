#include "battery_life.h"

#include <math.h>

bool recordBatteryVoltage(BatteryLifeState& state, uint32_t now, float voltage) {
    if (state.readingCount != 0 &&
        now - state.lastReadingAt < BATTERY_READING_SECONDS) {
        return false;
    }

    bool reset = false;
    if (state.count != 0) {
        float lowest = state.history[0].voltage;
        for (uint32_t i = 1; i < state.count; ++i) {
            if (state.history[i].voltage < lowest) lowest = state.history[i].voltage;
        }
        state.rechargeReadings = voltage >= lowest + 0.15f
            ? state.rechargeReadings + 1 : 0;
        if (state.rechargeReadings >= 3) {
            state = {};
            reset = true;
        }
    }

    if (state.readingCount != 0 &&
        now - state.bucketStartedAt >= BATTERY_BUCKET_SECONDS) {
        BatterySample& sample = state.history[state.next];
        sample.seconds = state.bucketStartedAt + state.offsetSum / state.readingCount;
        sample.voltage = state.voltageSum / state.readingCount;
        state.next = (state.next + 1) % BATTERY_HISTORY_SIZE;
        if (state.count < BATTERY_HISTORY_SIZE) ++state.count;
        state.readingCount = 0;
        state.offsetSum = 0;
        state.voltageSum = 0;
    }

    if (state.readingCount == 0) state.bucketStartedAt = now;
    state.lastReadingAt = now;
    state.offsetSum += now - state.bucketStartedAt;
    state.voltageSum += voltage;
    ++state.readingCount;
    return reset;
}

int estimateBatteryDays(const BatteryLifeState& state, uint32_t now,
                        float voltage, float cutoff) {
    if (voltage <= cutoff) return 0;
    if (state.count < 5 || state.rechargeReadings != 0) return -1;

    uint32_t oldest = (state.next + BATTERY_HISTORY_SIZE - state.count) % BATTERY_HISTORY_SIZE;
    uint32_t newest = (state.next + BATTERY_HISTORY_SIZE - 1) % BATTERY_HISTORY_SIZE;
    uint32_t start = state.history[oldest].seconds;
    uint32_t span = state.history[newest].seconds - start;
    if (span < 24 * 60 * 60 ||
        state.history[oldest].voltage - state.history[newest].voltage < 0.01f) {
        return -1;
    }

    double meanDays = 0;
    double meanVoltage = 0;
    for (uint32_t i = 0; i < state.count; ++i) {
        const BatterySample& sample = state.history[(oldest + i) % BATTERY_HISTORY_SIZE];
        meanDays += (sample.seconds - start) / 86400.0;
        meanVoltage += sample.voltage;
    }
    meanDays /= state.count;
    meanVoltage /= state.count;

    double covariance = 0;
    double variance = 0;
    for (uint32_t i = 0; i < state.count; ++i) {
        const BatterySample& sample = state.history[(oldest + i) % BATTERY_HISTORY_SIZE];
        double days = (sample.seconds - start) / 86400.0 - meanDays;
        covariance += days * (sample.voltage - meanVoltage);
        variance += days * days;
    }
    double voltsPerDay = covariance / variance;
    if (voltsPerDay >= 0 || -voltsPerDay * span / 86400.0 < 0.01) return -1;

    // Extrapolate to now rather than treating the last six-hour average as
    // an instantaneous reading; this also avoids ADC noise in the day count.
    double currentVoltage = meanVoltage + voltsPerDay * ((now - start) / 86400.0 - meanDays);
    if (currentVoltage <= cutoff) return 0;
    return static_cast<int>(round((currentVoltage - cutoff) / -voltsPerDay));
}
