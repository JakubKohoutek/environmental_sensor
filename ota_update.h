#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

constexpr unsigned long OTA_WAIT_MS = 5UL * 60 * 1000;
constexpr float OTA_MIN_VOLTAGE = 3.6f;

/** Publishes the queued-update button and last OTA status discovery entries. */
void publishOtaDiscovery(const char* deviceJson);

/** Checks a retained request and returns true if the OTA screen was used. */
bool checkOtaRequest(float batteryVoltage);

#endif
