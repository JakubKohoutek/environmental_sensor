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
#define MQTT_OTA_COMMAND_TOPIC    MQTT_BASE_TOPIC "/ota/set"
#define MQTT_OTA_STATUS_TOPIC     MQTT_BASE_TOPIC "/ota/status"

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

/** Connects MQTT with the configured credentials, last will, and discovery buffer. */
bool connectMqtt();

#endif // MQTT_H
