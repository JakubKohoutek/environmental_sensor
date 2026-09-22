#include "../battery_life.h"

#include <assert.h>
#include <initializer_list>
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

    state = discharge(24, 0.02f);
    assert(estimateBatteryDays(state, 24 * 3600, 4.08f, 3.5f) == -1);
    state = discharge(30, 0.02f);
    assert(state.count == 5);
    assert(estimateBatteryDays(state, 30 * 3600, 4.075f, 3.5f) == 29);

    state = discharge(10 * 24, 0.02f);
    assert(state.count == BATTERY_HISTORY_SIZE);
    assert(estimateBatteryDays(state, 10 * 86400, 3.9f, 3.5f) == 20);

    BatteryLifeState frequent = discharge(30, 0.02f, 3);
    BatteryLifeState idle = discharge(30, 0.02f, 10);
    assert(frequent.count == idle.count);
    assert(frequent.readingCount == idle.readingCount);
    assert(fabs(frequent.history[0].voltage - idle.history[0].voltage) < 0.00001f);

    for (float rate : {0.0f, -0.01f, 0.001f}) {
        state = discharge(48, rate);
        assert(estimateBatteryDays(state, 48 * 3600, 4.1f, 3.5f) == -1);
    }

    state = {};
    for (uint32_t time = 0; time <= 48 * 3600; time += 300) {
        float noise = (time / 300) % 2 ? 0.015f : -0.015f;
        recordBatteryVoltage(state, time, 4.1f - 0.02f * time / 86400.0f + noise);
    }
    assert(estimateBatteryDays(state, 48 * 3600, 4.06f, 3.5f) == 28);

    state = {};
    for (uint32_t time = 0; time <= 48 * 3600; time += 300) {
        float noise = (time / 300) % 2 ? 0.015f : -0.015f;
        recordBatteryVoltage(state, time, 4.0f + noise);
    }
    assert(estimateBatteryDays(state, 48 * 3600, 4.0f, 3.5f) == -1);

    state = discharge(48, 0.02f);
    for (uint32_t seconds = 48 * 3600 + 300; seconds <= 48 * 3600 + 900; seconds += 300) {
        bool reset = recordBatteryVoltage(state, seconds, 4.25f);
        assert(reset == (seconds == 48 * 3600 + 900));
        assert(estimateBatteryDays(state, seconds, 4.25f, 3.5f) == -1);
    }
    assert(state.count == 0 && state.readingCount == 1);

    state = discharge(48, 0.02f);
    assert(!recordBatteryVoltage(state, 48 * 3600 + 300, 4.25f));
    assert(!recordBatteryVoltage(state, 48 * 3600 + 600, 4.06f));
    assert(state.count == 8 && state.rechargeReadings == 0);

    uint32_t start = UINT32_MAX - 12 * 3600;
    state = discharge(48, 0.02f, 300, start);
    assert(estimateBatteryDays(state, start + 48 * 3600, 4.06f, 3.5f) == 28);

    state = discharge(48, 0.02f);
    assert(estimateBatteryDays(state, 48 * 3600, 3.49f, 3.5f) == 0);

    puts("Battery-life tests passed");
}
