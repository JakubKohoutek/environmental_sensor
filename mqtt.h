#ifndef MQTT_H
#define MQTT_H

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define MQTT_BASE_TOPIC       "environmental_sensor"
#define MQTT_AVAILABLE_TOPIC  MQTT_BASE_TOPIC "/available"

#define MQTT_TEMPERATURE_TOPIC    MQTT_BASE_TOPIC "/temperature"
#define MQTT_HUMIDITY_TOPIC       MQTT_BASE_TOPIC "/humidity"
#define MQTT_PRESSURE_TOPIC       MQTT_BASE_TOPIC "/pressure"
#define MQTT_ALTITUDE_TOPIC       MQTT_BASE_TOPIC "/altitude"
#define MQTT_BATTERY_TOPIC        MQTT_BASE_TOPIC "/battery"
#define MQTT_MOTION_TOPIC         MQTT_BASE_TOPIC "/motion"

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

#endif // MQTT_H
