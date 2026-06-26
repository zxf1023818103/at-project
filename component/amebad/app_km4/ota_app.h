/*
 * ota_app.h - AWS IoT OTA Agent integration over the shared coreMQTT-Agent
 * connection (make AWS=ON).
 *
 * ota_app_init() builds the OTA OS/MQTT/PAL interfaces (the MQTT plane runs through
 * the same MQTTAgentContext_t the telemetry task uses) and starts the OTA agent task.
 * It must be called AFTER the MQTT-Agent connection is up (so the OTA subscribes land
 * on a live session). Call ota_app_on_mqtt_connected() each time (re)connection
 * completes to (re)start the OTA job check on the shared connection.
 */
#ifndef OTA_APP_H
#define OTA_APP_H

#include "core_mqtt_agent.h"

/**
 * @brief Initialise + start the OTA agent. Call once, after the first MQTT-Agent
 *        connection is established.
 *
 * @param[in] pAgent  The shared MQTT-Agent context (owned by aws_iot_demo.c).
 * @param[in] pThingName  NUL-terminated Thing name (= BLE MAC); used as the OTA client id.
 *
 * @return 0 on success, <0 on failure.
 */
int ota_app_init( MQTTAgentContext_t * pAgent, const char * pThingName );

/**
 * @brief Notify the OTA agent that the MQTT-Agent connection is (re)established, so it
 *        (re)starts / resumes the job check. Safe to call on every reconnect.
 */
void ota_app_on_mqtt_connected( void );

/**
 * @brief True while an OTA file download is in progress (CreatingFile..ClosingFile). The telemetry
 *        task goes quiet during this window so its publish churn doesn't fragment the heap that the
 *        OTA HTTPS (2nd TLS) handshake/records need.
 */
int ota_app_is_active( void );

#endif /* OTA_APP_H */
