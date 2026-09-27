#include "../battery_life.h"

#include <assert.h>
#include <limits.h>
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

    recordBatteryVoltage(state, 0, 4.1f);
    assert(!recordBatteryVoltage(state, 3, 3.9f));
    assert(!state.hasFirstDrop && state.lastReadingAt == 0);
    recordBatteryVoltage(state, 300, 4.1f);
    assert(!state.hasFirstDrop);
    recordBatteryVoltage(state, 600, 4.09f);
    assert(state.hasFirstDrop && state.firstDropAt == 600);
    assert(estimateBatteryDays(state, 600, 4.09f, 3.5f) == -1);
    recordBatteryVoltage(state, 900, 4.08f);
    assert(estimateBatteryDays(state, 900, 4.08f, 3.5f) == 0);
    assert(estimateBatteryDays(state, 900, 4.09f, 3.5f) == -1);
    assert(estimateBatteryDays(state, 900, 4.1f, 3.5f) == -1);

    state = discharge(30, 0.02f);
    assert(state.firstDropAt == 300);
    assert(estimateBatteryDays(state, 30 * 3600, 4.075f, 3.5f) == 29);

    state = discharge(10 * 24, 0.02f);
    assert(state.firstDropAt == 300);
    assert(estimateBatteryDays(state, 10 * 86400, 3.9f, 3.5f) == 20);

    BatteryLifeState frequent = discharge(30, 0.02f, 3);
    BatteryLifeState idle = discharge(30, 0.02f, 10);
    assert(frequent.firstDropAt == idle.firstDropAt);
    assert(frequent.firstDropVoltage == idle.firstDropVoltage);

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
    assert(state.hasReading && !state.hasFirstDrop && state.initialVoltage == 4.25f);

    state = discharge(48, 0.02f);
    assert(!recordBatteryVoltage(state, 48 * 3600 + 300, 4.25f));
    assert(!recordBatteryVoltage(state, 48 * 3600 + 600, 4.06f));
    assert(state.hasFirstDrop && state.rechargeReadings == 0);

    uint32_t start = UINT32_MAX - 12 * 3600;
    state = discharge(48, 0.02f, 300, start);
    assert(estimateBatteryDays(state, start + 48 * 3600, 4.06f, 3.5f) == 28);

    state = discharge(48, 0.02f);
    assert(estimateBatteryDays(state, 48 * 3600, 3.49f, 3.5f) == 0);

    state = {};
    recordBatteryVoltage(state, 0, 4.1f);
    recordBatteryVoltage(state, 300, 4.099999f);
    assert(estimateBatteryDays(state, UINT32_MAX, 4.099998f, 3.5f) == INT_MAX);

    puts("Battery-life tests passed");
}
