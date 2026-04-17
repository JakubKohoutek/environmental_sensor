#ifndef SENSORS_H
#define SENSORS_H

struct SensorData {
    float temperature;      // DHT22 temperature (°C), calibrated
    float humidity;         // DHT22 relative humidity (%), calibrated
    float pressure;         // BMP180 station pressure (hPa)
    float seaLevelPressure; // Sea-level adjusted pressure (hPa)
    float altitude;         // BMP180 estimated altitude (m)
    float bmpTemp;          // BMP180 temperature (°C), calibrated
    bool  dhtOk;            // DHT22 read success
    bool  bmpOk;            // BMP180 read success
};

extern SensorData sensorData;

void initiateSensors();
void readSensors(float tempOffset = 0.0);

#endif // SENSORS_H
