#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

#include "sensors.h"
#include "debug.h"

// Station altitude for sea-level pressure calculation
#define STATION_ALTITUDE_M  235.0

// BMP280 lives at 0x76 on most GY-AHT20+BMP280 combo boards (SDO pulled
// low on-board). Some boards use 0x77; initiateSensors() tries both.
#define BMP280_ADDR_PRIMARY   0x76
#define BMP280_ADDR_SECONDARY 0x77

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

SensorData sensorData = {0, 0, 0, 0, 0, 0, false, false};

void initiateSensors() {
    if (!aht.begin()) {
        DBG_PRINTLN("[SENSORS] AHT20 not found at 0x38!");
        sensorData.ahtOk = false;
    } else {
        DBG_PRINTLN("[SENSORS] AHT20 initialized");
    }

    bool bmpFound = bmp.begin(BMP280_ADDR_PRIMARY);
    if (!bmpFound) {
        bmpFound = bmp.begin(BMP280_ADDR_SECONDARY);
        if (bmpFound) {
            DBG_PRINTLN("[SENSORS] BMP280 initialized at 0x77");
        }
    } else {
        DBG_PRINTLN("[SENSORS] BMP280 initialized at 0x76");
    }

    if (!bmpFound) {
        DBG_PRINTLN("[SENSORS] BMP280 not found at 0x76 or 0x77!");
        sensorData.bmpOk = false;
    } else {
        sensorData.bmpOk = true;
        // FORCED mode: take one measurement per readSensors() call, then
        // the chip returns to sleep (~0.1 µA) — essential for battery life.
        bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                        Adafruit_BMP280::SAMPLING_X1,   // temperature oversample
                        Adafruit_BMP280::SAMPLING_X4,   // pressure oversample
                        Adafruit_BMP280::FILTER_OFF,
                        Adafruit_BMP280::STANDBY_MS_1);
    }
}

// Compensate humidity for temperature offset using Magnus formula.
// No-op when tempOffset == 0 (rawTemp == correctedTemp).
float compensateHumidityForTemp(float rawHumidity, float rawTemp, float correctedTemp) {
    float esRaw  = 6.112 * exp((17.67 * rawTemp)       / (rawTemp + 243.5));
    float esCorr = 6.112 * exp((17.67 * correctedTemp)  / (correctedTemp + 243.5));
    return rawHumidity * esRaw / esCorr;
}

// Linear humidity calibration anchored at 100%.
// AHT20 is factory-calibrated to ±2% RH, so the factor defaults to 1.0
// (pass-through). Adjust if you measure a bias against a reference.
// Formula: actual = 100 - (100 - raw) * HUMIDITY_CAL_FACTOR
#define HUMIDITY_CAL_FACTOR  1.0

float calibrateHumidity(float humidity) {
    float calibrated = 100.0 - (100.0 - humidity) * HUMIDITY_CAL_FACTOR;
    if (calibrated < 0) calibrated = 0;
    if (calibrated > 100) calibrated = 100;
    return calibrated;
}

// Convert station pressure to sea-level pressure using barometric formula
float toSeaLevelPressure(float stationPressure) {
    return stationPressure / pow(1.0 - (STATION_ALTITUDE_M / 44330.0), 5.255);
}

// Zambretti weather forecast algorithm
// Uses sea-level pressure and trend to predict weather
// Returns a short Czech string suitable for small OLED display
// All strings must fit in 64px at 7x13B font (max 9 chars)
const char* zambretti(float p, int trend) {
    if (trend > 0) {
        // Rising pressure — weather improving
        if (p > 1030) return "Ustaleno";
        if (p > 1022) return "Jasno";
        if (p > 1012) return "Pekne";
        if (p > 1003) return "Vyjasni";
        if (p >  993) return "Prehanky";
        return "Brzy dest";
    }
    if (trend < 0) {
        // Falling pressure — weather deteriorating
        if (p > 1030) return "Pekne";
        if (p > 1022) return "Nestale";
        if (p > 1012) return "Brzy dest";
        if (p > 1003) return "Dest";
        if (p >  993) return "Bourky";
        return "Boure!";
    }
    // Steady pressure
    if (p > 1030) return "Jasno";
    if (p > 1022) return "Pekne";
    if (p > 1012) return "OK";
    if (p > 1003) return "Prehanky";
    if (p >  993) return "Dest";
    return "Bourky";
}

void readSensors(float tempOffset) {
    // AHT20 — getEvent() triggers a measurement (~80ms), sensor returns to idle after
    sensors_event_t ahtHumidity, ahtTemp;
    if (aht.getEvent(&ahtHumidity, &ahtTemp)) {
        float t = ahtTemp.temperature;
        float h = ahtHumidity.relative_humidity;

        float correctedTemp = t - tempOffset;
        float correctedHum  = calibrateHumidity(compensateHumidityForTemp(h, t, correctedTemp));

        sensorData.ahtOk       = true;
        sensorData.temperature = correctedTemp;
        sensorData.humidity    = correctedHum;

        DBG_PRINTLN("[AHT20] raw T:" + String(t, 1) + "C H:" + String(h, 1) +
                       "% → corrected T:" + String(correctedTemp, 1) + "C H:" + String(correctedHum, 1) + "%");
    } else {
        DBG_PRINTLN("[AHT20] Read failed");
        sensorData.ahtOk = false;
    }

    // BMP280 — trigger a forced measurement, then read values
    if (sensorData.bmpOk) {
        if (bmp.takeForcedMeasurement()) {
            sensorData.bmpTemp          = bmp.readTemperature() - tempOffset;
            sensorData.pressure         = bmp.readPressure() / 100.0; // Pa -> hPa
            sensorData.seaLevelPressure = toSeaLevelPressure(sensorData.pressure);
            sensorData.altitude         = bmp.readAltitude();
            DBG_PRINTLN("[BMP280] T:" + String(sensorData.bmpTemp, 1) +
                           "C P:" + String(sensorData.pressure, 1) +
                           "hPa sea:" + String(sensorData.seaLevelPressure, 1) + "hPa");
        } else {
            DBG_PRINTLN("[BMP280] Forced measurement failed");
            sensorData.bmpOk = false;
        }
    }
}
