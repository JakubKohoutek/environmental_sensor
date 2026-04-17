#include "mqtt.h"

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
