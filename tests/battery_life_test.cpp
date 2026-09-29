#include "../battery_life.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static BatteryLifeState discharge(uint32_t hours, float voltsPerDay,
                                  uint32_t step = 300, uint32_t start = 0) {
    BatteryLifeState state = {};
    for (uint32_t seconds = 0; seconds <= hours * 3600; seconds += step) {
        recordBatteryVoltage(state, start + seconds, 4.1f - voltsPerDay * seconds / 86400.0f);
    }
    return state;
}

int main() {
    BatteryLifeState state = {};
    assert(estimateBatteryDays(state, 0, 4.1f, 3.5f) == -1);
    assert(estimateBatteryDays(state, 0, 3.5f, 3.5f) == 0);

    for (uint32_t i = 0; i < BATTERY_AVERAGE_SAMPLES - 1; ++i) {
        recordBatteryVoltage(state, i * 300, 4.1f);
        assert(!state.hasFirstDrop && state.initialVoltage == 0);
    }
    recordBatteryVoltage(state, (BATTERY_AVERAGE_SAMPLES - 1) * 300, 4.1f);
    assert(state.initialVoltage == 4.1f && state.readingCount == BATTERY_AVERAGE_SAMPLES);
    uint32_t lastAt = state.lastReadingAt;
    assert(!recordBatteryVoltage(state, lastAt + 3, 3.9f));
    assert(state.lastReadingAt == lastAt && state.smoothedVoltage == 4.1f);

    // Alternating ADC noise must not start the discharge baseline.
    for (uint32_t i = 6; i < 30; ++i) {
        recordBatteryVoltage(state, i * 300, i % 2 ? 4.09f : 4.11f);
        assert(!state.hasFirstDrop);
        assert(estimateBatteryDays(state, i * 300, 4.09f, 3.5f) == -1);
    }

    uint32_t firstDrop = 0;
    for (uint32_t i = 30; i < 45; ++i) {
        recordBatteryVoltage(state, i * 300, 4.08f);
        if (state.hasFirstDrop && !firstDrop) firstDrop = state.firstDropAt;
        assert(estimateBatteryDays(state, i * 300, 4.08f, 3.5f) == -1);
    }
    assert(firstDrop && state.firstDropAt == firstDrop);
    float baseline = state.firstDropVoltage;
    for (uint32_t i = 45; i < 60; ++i) {
        recordBatteryVoltage(state, i * 300, 4.05f);
    }
    assert(state.firstDropAt == firstDrop && state.firstDropVoltage == baseline);
    assert(estimateBatteryDays(state, 59 * 300, 4.05f, 3.5f) >= 0);

    state = discharge(30, 0.02f);
    assert(state.hasFirstDrop);
    assert(estimateBatteryDays(state, 30 * 3600, 4.075f, 3.5f) == -1);
    state = discharge(48, 0.02f);
    assert(state.hasFirstDrop);
    assert(estimateBatteryDays(state, 48 * 3600, 4.06f, 3.5f) >= 26);
    assert(estimateBatteryDays(state, 48 * 3600, 4.06f, 3.5f) <= 30);

    state = discharge(10 * 24, 0.02f);
    assert(estimateBatteryDays(state, 10 * 86400, 3.9f, 3.5f) >= 19);
    assert(estimateBatteryDays(state, 10 * 86400, 3.9f, 3.5f) <= 21);

    BatteryLifeState frequent = discharge(48, 0.02f, 3);
    BatteryLifeState idle = discharge(48, 0.02f, 10);
    assert(frequent.firstDropAt == idle.firstDropAt);
    assert(fabs(frequent.smoothedVoltage - idle.smoothedVoltage) < 0.00001f);

    state = discharge(48, 0.0f);
    assert(estimateBatteryDays(state, 48 * 3600, 4.1f, 3.5f) == -1);
    state = discharge(48, -0.01f);
    assert(estimateBatteryDays(state, 48 * 3600, 4.12f, 3.5f) == -1);

    state = discharge(48, 0.02f);
    for (uint32_t seconds = 48 * 3600 + 300; seconds <= 48 * 3600 + 900; seconds += 300) {
        bool reset = recordBatteryVoltage(state, seconds, 4.25f);
        assert(reset == (seconds == 48 * 3600 + 900));
        assert(estimateBatteryDays(state, seconds, 4.25f, 3.5f) == -1);
    }
    assert(state.hasReading && !state.hasFirstDrop && state.readingCount == 1);

    state = discharge(48, 0.02f);
    assert(!recordBatteryVoltage(state, 48 * 3600 + 300, 4.25f));
    assert(!recordBatteryVoltage(state, 48 * 3600 + 600, 4.06f));
    assert(state.hasFirstDrop && state.rechargeReadings == 0);

    uint32_t start = UINT32_MAX - 12 * 3600;
    state = discharge(48, 0.02f, 300, start);
    assert(estimateBatteryDays(state, start + 48 * 3600, 4.06f, 3.5f) >= 26);
    assert(estimateBatteryDays(state, start + 48 * 3600, 4.06f, 3.5f) <= 30);
    assert(estimateBatteryDays(state, start + 48 * 3600, 3.49f, 3.5f) == 0);

    puts("Battery-life tests passed");
}
