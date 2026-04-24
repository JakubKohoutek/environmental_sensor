#ifndef SENSORS_H
#define SENSORS_H

struct SensorData {
    float temperature;      // AHT20 temperature (°C), calibrated
    float humidity;         // AHT20 relative humidity (%), calibrated
    float pressure;         // BMP280 station pressure (hPa)
    float seaLevelPressure; // Sea-level adjusted pressure (hPa)
    float altitude;         // BMP280 estimated altitude (m)
    float bmpTemp;          // BMP280 temperature (°C), calibrated
    bool  ahtOk;            // AHT20 read success
    bool  bmpOk;            // BMP280 read success
};

extern SensorData sensorData;

void initiateSensors();
void readSensors(float tempOffset = 0.0);

// Zambretti weather forecast based on sea-level pressure and trend
// trend: 1=rising, 0=steady, -1=falling
const char* zambretti(float seaLevelPressure, int trend);

#endif // SENSORS_H
