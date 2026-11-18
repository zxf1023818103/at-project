/*
 * aws_telemetry.c - the lean AWS IoT Core telemetry client (AWS=ON on BOTH platforms).
 *
 * Direct coreMQTT over the switchable TLS transport (component/network_transport) - NO
 * coreMQTT-Agent, NO OTA agent (that's the AWSOTA=ON image, aws_iot_demo.c). Mutual TLS to AWS
 * IoT Core :8883, server trust = CA bundle/file, client cert/key provisioned to LittleFS (or the
 * key in TF-M ITS on AmebaD TFM images), endpoint from env "endpoint". Publishes the shared
 * telemetry JSON to bw20/<mac>/telemetry every period.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "core_mqtt.h"
#include "transport_tls.h"        /* NetworkContext_t / NetworkCredentials_t / TLS_FreeRTOS_* */

#include "aws_iot.h"
#include "aws_connect.h"          /* shared TLS+MQTT connect handshake */
#include "aws_telemetry_common.h" /* shared topic (bw20/) + JSON schema, same as aws_iot_demo.c */

/* --- config (AWS_MQTT_PORT comes from aws_iot_config.h via aws_telemetry_common.h) ---------- */
#define AWS_NET_BUFFER_SZ      2048U            /* telemetry PUBLISH is small; 2 KB fixed buffer */
#define AWS_KEEPALIVE_S        60U
#define AWS_CONNECT_TMO_MS     20000U
#define AWS_RECV_TMO_MS        1000U
#define AWS_SEND_TMO_MS        2000U
#define AWS_PUBLISH_PERIOD_MS  10000U

/* transport_mbedtls.c calls this from its connect path to arm mbedTLS's FreeRTOS threading. */
extern void mbedtls_threading_freertos_init(void);
void mbedtls_platform_threading_init(void) { mbedtls_threading_freertos_init(); }

/* coreMQTT leaves `struct NetworkContext` for the application to define (opaque in the library).
 * The mbedTLS transport stores its per-connection state through a TlsTransportParams_t pointer. */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

static NetworkContext_t    s_net;
static TlsTransportParams_t s_tlsParams;
static MQTTContext_t       s_mqtt;
static uint8_t             s_netBuf[ AWS_NET_BUFFER_SZ ];
static char                s_endpoint[ 128 ];
static char                s_mac[ 13 ];         /* "aabbccddeeff" */
static char                s_topic[ 48 ];       /* bw20/<mac>/telemetry (AWS_TOPIC_TELEMETRY_FMT) */
static uint8_t             s_key[ AWS_KEY_MAX ];
static size_t              s_keyLen;
static NetworkCredentials_t s_creds;

/* coreMQTT needs a monotonic ms clock; newlib time_base gives real UTC after SNTP, but for MQTT
 * timing a free-running tick-based ms is what matters (keepalive / timeouts). */
static uint32_t prvGetTimeMs(void)
{
    return (uint32_t) ((uint64_t) xTaskGetTickCount() * 1000u / configTICK_RATE_HZ);
}

/* No subscriptions in this telemetry-only client; a stray incoming packet is just ignored.
 * coreMQTT v5.0.2 event callback (returns bool; the extra params are the MQTT-5 reason code +
 * property builders, all unused here). */
static bool prvEventCb(MQTTContext_t *pCtx, MQTTPacketInfo_t *pPacketInfo, MQTTDeserializedInfo_t *pDeser,
                       MQTTSuccessFailReasonCode_t *pReason, MQTTPropBuilder_t *pSendProps,
                       MQTTPropBuilder_t *pGetProps)
{
    (void) pCtx; (void) pPacketInfo; (void) pDeser; (void) pReason; (void) pSendProps; (void) pGetProps;
    return true;
}

/* Load creds + the shared TLS handshake (aws_connect_tls), then MQTT_Init + MQTT_Connect. 0 = connected. */
static int prvConnect(void)
{
    const char  *ep = getenv("endpoint");
    const char  *certPath = getenv("cert");
    const char  *caPath = getenv("ca");
    TransportInterface_t transport;
    MQTTFixedBuffer_t    netBuf;
    MQTTConnectInfo_t    conn;
    MQTTStatus_t         st;
    bool                 sessionPresent = false;
    aws_connect_params_t p;

    if (ep == NULL || ep[0] == '\0') {
        printf("[AWS] endpoint not set (setenv endpoint <xxx-ats.iot.<region>.amazonaws.com>)\r\n");
        return -1;
    }
    if (certPath == NULL || certPath[0] == '\0') certPath = AWS_CERT_PATH;
    if (caPath   == NULL || caPath[0]   == '\0') caPath   = AWS_CA_PATH;
    if (aws_load_key((char *) s_key, sizeof(s_key), &s_keyLen) != 0) {
        printf("[AWS] no client key provisioned (aws key ...)\r\n");
        return -1;
    }
    strncpy(s_endpoint, ep, sizeof(s_endpoint) - 1);
    s_net.pParams = &s_tlsParams;

    /* Shared handshake: NetworkCredentials fill + TLS_FreeRTOS_Connect + transport/connectInfo fill.
     * bl602 = the default TLS backend (no rxReserve / picotls forcing), CA is a single LittleFS file. */
    memset(&p, 0, sizeof(p));
    p.endpoint = s_endpoint;  p.certPath = certPath;  p.caPath = caPath;
    p.key = s_key;  p.keyLen = s_keyLen;  p.clientId = s_mac;
    p.port = AWS_MQTT_PORT;  p.recvTmoMs = AWS_RECV_TMO_MS;  p.sendTmoMs = AWS_SEND_TMO_MS;
    p.keepAliveS = AWS_KEEPALIVE_S;  p.rxReserveSize = 0;  p.backendMode = 0;
    if (aws_connect_tls(&s_net, &s_creds, &transport, &conn, &p) != 0) {
        return -1;   /* aws_connect_tls logged the failure + tore down the socket */
    }

    netBuf.pBuffer = s_netBuf;
    netBuf.size    = sizeof(s_netBuf);

    st = MQTT_Init(&s_mqtt, &transport, prvGetTimeMs, prvEventCb, &netBuf);
    if (st != MQTTSuccess) { printf("[AWS] MQTT_Init FAIL (%d)\r\n", (int) st); goto fail; }

    /* coreMQTT v5.0.2: the two trailing args are the MQTT-5 CONNECT / will property builders (unused). */
    st = MQTT_Connect(&s_mqtt, &conn, NULL, AWS_CONNECT_TMO_MS, &sessionPresent, NULL, NULL);
    if (st != MQTTSuccess) { printf("[AWS] MQTT_Connect FAIL (%d)\r\n", (int) st); goto fail; }

    printf("[AWS] connected to AWS IoT Core as '%s'\r\n", s_mac);
    return 0;

fail:
    TLS_FreeRTOS_Disconnect(&s_net);
    return -1;
}

static int prvPublishTelemetry(uint32_t seq)
{
    char              payload[ 160 ];
    MQTTPublishInfo_t pub;
    int               n;

    n = aws_build_telemetry_json(payload, sizeof(payload), s_mac, (unsigned) seq);
    if (n <= 0) return -1;

    memset(&pub, 0, sizeof(pub));
    pub.qos = MQTTQoS0;                          /* QoS0: no stateful-QoS records, minimal heap */
    pub.pTopicName = s_topic;
    pub.topicNameLength = (uint16_t) strlen(s_topic);
    pub.pPayload = payload;
    pub.payloadLength = (size_t) n;

    /* coreMQTT v5.0.2: (ctx, pub, packetId, propertyBuilder). QoS0 needs no packet ID; no MQTT-5 props. */
    if (MQTT_Publish(&s_mqtt, &pub, 0, NULL) != MQTTSuccess) return -1;
    printf("[AWS] TX %s %s\r\n", s_topic, payload);   /* same line as aws_iot_demo.c */
    return 0;
}

static void prvTelemetryTask(void *arg)
{
    uint32_t seq = 0;
    (void) arg;

    /* Install the mbedTLS THREADING_ALT mutex funcs once, before any TLS/PSA use. The mbedTLS
     * transport does this in its connect path, but the picotls backend (which calls psa_crypto_init,
     * and PSA takes mbedTLS mutexes) does NOT -- so without this, TLS_BACKEND=picotls fails at
     * psa_crypto_init with PSA_ERROR_SERVICE_FAILURE (-144). Idempotent; harmless for the mbedTLS path. */
    mbedtls_platform_threading_init();

    /* Thing name / client ID / topic = the BT MAC (shared aws_get_mac over at_netif_bt_mac). */
    if (aws_get_mac(s_mac, sizeof(s_mac)) != 0) strcpy(s_mac, "unknown-mac");
    snprintf(s_topic, sizeof(s_topic), AWS_TOPIC_TELEMETRY_FMT, s_mac);   /* bw20/<mac>/telemetry */

    for (;;) {
        if (prvConnect() != 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));     /* retry the whole connect on failure */
            continue;
        }
        /* Connected: publish telemetry every period, driving the MQTT keepalive between publishes. */
        for (;;) {
            if (prvPublishTelemetry(seq) == 0) {
                seq++;
            } else {
                printf("[AWS] publish failed -> reconnect\r\n");
                break;
            }
            /* Keep the connection alive across the idle period (process CONNACK/PINGRESP). */
            if (MQTT_ProcessLoop(&s_mqtt) != MQTTSuccess) { printf("[AWS] process loop failed -> reconnect\r\n"); break; }
            vTaskDelay(pdMS_TO_TICKS(AWS_PUBLISH_PERIOD_MS));
        }
        TLS_FreeRTOS_Disconnect(&s_net);
    }
}

/* True once the device has been provisioned (endpoint + client key). The network-up hook calls
 * aws_telemetry_start() unconditionally, so this gate keeps an UNprovisioned device from spawning a
 * task that just fails-and-retries forever. `aws start` also honours it (nothing to start until set). */
int aws_is_provisioned(void)
{
    const char *ep = getenv("endpoint");
    const char *ke = getenv("key");
    return (ep != NULL && ep[0] != '\0' && ke != NULL && ke[0] != '\0');
}

void aws_telemetry_start(void)
{
    static int started = 0;
    if (started) return;
    if (!aws_is_provisioned()) {
        printf("[AWS] not provisioned (run `aws endpoint/ca/cert/key ...`); telemetry not started\r\n");
        return;
    }
    started = 1;
    /* 8 KB stack (words*4): the mbedTLS TLS handshake (X.509 chain parse + RSA-2048 verify + ECDHE)
     * is stack-heavy - 4 KB overflowed during the handshake. Big TLS record buffers live on the heap;
     * this is just the call-depth headroom. Started automatically from the network-up hook once WiFi +
     * DHCP are up; the connect loop self-heals until SNTP gives real UTC (mbedTLS cert-time check). */
    xTaskCreate(prvTelemetryTask, "aws_tel", 2048, NULL, tskIDLE_PRIORITY + 3, NULL);
}

/* Platform verb for the shared `aws` CLI (aws_cli.c): `aws start` kicks the telemetry task
 * (also auto-started on network-up, so this is mostly a manual re-trigger). WEAK: on AmebaD this
 * TU now links into the lean AWS image alongside aws_platform.c, whose strong aws_cli_platform
 * (blemac/secchk) wins -- amebad keeps its verbs, bl602 (no strong override) keeps `aws start`. */
__attribute__((weak))
int aws_cli_platform(const char *sub, size_t sublen, const char *arg, size_t arglen,
                     char *out, size_t outlen)
{
    (void) arg; (void) arglen;
    if (sublen == 5 && strncmp(sub, "start", 5) == 0) {
        aws_telemetry_start();
        snprintf(out, outlen, "aws telemetry start requested\r\n");
        return 0;
    }
    return -1;
}
