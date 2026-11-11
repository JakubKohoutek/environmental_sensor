#include "mqtt.h"
#include "debug.h"
#include <credentials.h>

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool connectMqtt() {
    if (!mqttClient.setBufferSize(768)) return false;
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setSocketTimeout(2);

    String clientId = "ENV_SENSOR_" + String(ESP.getChipId(), HEX);
    bool connected;
    #ifdef MQTT_USER
        connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                                        MQTT_AVAILABLE_TOPIC, 0, true, "offline");
    #else
        connected = mqttClient.connect(clientId.c_str(),
                                        MQTT_AVAILABLE_TOPIC, 0, true, "offline");
    #endif

    if (!connected) {
        DBG_PRINTLN("[MQTT] Connection failed, rc=" + String(mqttClient.state()));
    }
    return connected;
}
