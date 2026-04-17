#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>

#include "sensors.h"

#define DHT_PIN   D5
#define DHT_TYPE  DHT22

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP085 bmp;

SensorData sensorData = {0, 0, 0, 0, 0, false, false};

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
#define HUMIDITY_CAL_FACTOR  1.368  // (100 - 53.2) / (100 - 65.8)

float calibrateHumidity(float humidity) {
    float calibrated = 100.0 - (100.0 - humidity) * HUMIDITY_CAL_FACTOR;
    if (calibrated < 0) calibrated = 0;
    if (calibrated > 100) calibrated = 100;
    return calibrated;
}

void readSensors(float tempOffset) {
    // Read DHT22
    float h = dht.readHumidity();
    float t = dht.readTemperature();

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
        sensorData.bmpTemp  = bmp.readTemperature() - tempOffset;
        sensorData.pressure = bmp.readPressure() / 100.0; // Pa -> hPa
        sensorData.altitude = bmp.readAltitude();
        Serial.println("[BMP180] T:" + String(sensorData.bmpTemp, 1) + "C P:" + String(sensorData.pressure, 1) + "hPa");
    }
}
