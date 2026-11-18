#ifndef AWS_CONNECT_H
#define AWS_CONNECT_H

#include <stddef.h>
#include <stdint.h>

#include "core_mqtt.h"        /* MQTTConnectInfo_t, TransportInterface_t */
#include "transport_tls.h"    /* NetworkContext_t, NetworkCredentials_t, TLS_FreeRTOS_* */

#ifdef __cplusplus
extern "C" {
#endif

/* The connect inputs each platform resolves its own way (env / LittleFS files / device MAC). */
typedef struct
{
    const char *  endpoint;       /* AWS IoT ATS endpoint host                                   */
    const char *  certPath;       /* client cert path (LittleFS DER/PEM)                         */
    const char *  caPath;         /* server-trust CA path (a file, or a folder on AmebaD)        */
    const uint8_t *key;           /* client private key (DER, in RAM)                            */
    size_t        keyLen;
    const char *  clientId;       /* MQTT client id (device MAC)                                 */
    uint16_t      port;           /* 8883                                                        */
    uint32_t      recvTmoMs;      /* TLS recv timeout                                            */
    uint32_t      sendTmoMs;      /* TLS send timeout                                            */
    uint16_t      keepAliveS;     /* MQTT keep-alive                                             */
    size_t        rxReserveSize;  /* NetworkCredentials rxReserveSize (picotls); 0 = none        */
    int           backendMode;    /* TlsBackendMode_t (0 = default / fallback dispatcher)        */
} aws_connect_params_t;

/* The shared TLS+MQTT connect handshake used by BOTH the AmebaD coreMQTT-Agent client
 * (project/amebad/app_km4/aws_iot_demo.c) and the bl602 direct-coreMQTT telemetry client
 * (component/aws_iot_config/aws_telemetry.c): fills `creds` (mutual-TLS NetworkCredentials from `p`),
 * TLS_FreeRTOS_Connect on `net`, then fills `transport` (TLS_FreeRTOS_send/recv) and `connectInfo`
 * (clientId / keep-alive / clean session). Returns 0 on a TLS-up socket; the caller then does its own
 * MQTT(Agent)_Init(transport) + MQTT_Connect(connectInfo) (the divergent part). On failure the socket is
 * torn down (no leak on retry). `net->pParams`, `creds`, and the buffers referenced by `p` are
 * caller-owned and must outlive the connection. */
int aws_connect_tls( NetworkContext_t *      net,
                     NetworkCredentials_t *  creds,
                     TransportInterface_t *  transport,
                     MQTTConnectInfo_t *     connectInfo,
                     const aws_connect_params_t * p );

#ifdef __cplusplus
}
#endif

#endif /* AWS_CONNECT_H */
