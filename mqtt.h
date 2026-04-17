#ifndef MQTT_H
#define MQTT_H

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define MQTT_BASE_TOPIC       "environmental_sensor"
#define MQTT_CMD_TOPIC        MQTT_BASE_TOPIC "/command"
#define MQTT_STATE_TOPIC      MQTT_BASE_TOPIC "/state"
#define MQTT_AVAILABLE_TOPIC  MQTT_BASE_TOPIC "/available"
#define MQTT_EVENT_TOPIC      MQTT_BASE_TOPIC "/event"

#define MQTT_TEMPERATURE_TOPIC    MQTT_BASE_TOPIC "/temperature"
#define MQTT_HUMIDITY_TOPIC       MQTT_BASE_TOPIC "/humidity"
#define MQTT_PRESSURE_TOPIC       MQTT_BASE_TOPIC "/pressure"
#define MQTT_SEA_PRESSURE_TOPIC   MQTT_BASE_TOPIC "/sea_level_pressure"
#define MQTT_ALTITUDE_TOPIC       MQTT_BASE_TOPIC "/altitude"
#define MQTT_BATTERY_TOPIC        MQTT_BASE_TOPIC "/battery"
#define MQTT_MOTION_TOPIC         MQTT_BASE_TOPIC "/motion"

extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

#endif // MQTT_H
