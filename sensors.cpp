#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>

#include "sensors.h"

#define DHT_PIN   D5
#define DHT_TYPE  DHT22

// Station altitude for sea-level pressure calculation
#define STATION_ALTITUDE_M  235.0

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP085 bmp;

SensorData sensorData = {0, 0, 0, 0, 0, 0, false, false};

void initiateSensors() {
    dht.begin();
    Serial.println("[SENSORS] DHT22 initialized on pin D5");

    if (!bmp.begin()) {
        Serial.println("[SENSORS] BMP180 not found! Check wiring.");
        sensorData.bmpOk = false;
    } else {
        Serial.println("[SENSORS] BMP180 initialized");
        sensorData.bmpOk = true;
    }
}

// Compensate humidity for temperature offset using Magnus formula.
float compensateHumidityForTemp(float rawHumidity, float rawTemp, float correctedTemp) {
    float esRaw  = 6.112 * exp((17.67 * rawTemp)       / (rawTemp + 243.5));
    float esCorr = 6.112 * exp((17.67 * correctedTemp)  / (correctedTemp + 243.5));
    return rawHumidity * esRaw / esCorr;
}

// Linear humidity calibration anchored at 100%.
// Calibration point: raw 65.8% = actual 53.2%
// Formula: actual = 100 - (100 - raw) * HUMIDITY_CAL_FACTOR
#define HUMIDITY_CAL_FACTOR  1.269  // (100 - 43) / (100 - 55.07)

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
    // Read DHT22
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // DHT22 needs ~1-2s to stabilize after power-up; first read often returns NaN.
    // Retry once after the library's 2s minimum read interval.
    if (isnan(h) || isnan(t)) {
        Serial.println("[DHT22] First read failed, retrying...");
        delay(2100);
        h = dht.readHumidity();
        t = dht.readTemperature();
    }

    if (isnan(h) || isnan(t)) {
        Serial.println("[DHT22] Read failed — NaN (h=" + String(h) + " t=" + String(t) + ")");
        sensorData.dhtOk = false;
    } else {
        float correctedTemp = t - tempOffset;
        float correctedHum  = calibrateHumidity(compensateHumidityForTemp(h, t, correctedTemp));

        sensorData.dhtOk       = true;
        sensorData.temperature = correctedTemp;
        sensorData.humidity    = correctedHum;

        Serial.println("[DHT22] raw T:" + String(t, 1) + "C H:" + String(h, 1) +
                       "% → corrected T:" + String(correctedTemp, 1) + "C H:" + String(correctedHum, 1) + "%");
    }

    // Read BMP180
    if (sensorData.bmpOk) {
        sensorData.bmpTemp          = bmp.readTemperature() - tempOffset;
        sensorData.pressure         = bmp.readPressure() / 100.0; // Pa -> hPa
        sensorData.seaLevelPressure = toSeaLevelPressure(sensorData.pressure);
        sensorData.altitude         = bmp.readAltitude();
        Serial.println("[BMP180] T:" + String(sensorData.bmpTemp, 1) +
                       "C P:" + String(sensorData.pressure, 1) +
                       "hPa sea:" + String(sensorData.seaLevelPressure, 1) + "hPa");
    }
}
