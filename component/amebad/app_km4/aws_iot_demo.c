/*
 * aws_iot_demo.c — AmebaD KM4 AWS IoT Core MQTT/TLS client (make AWS=ON).
 *
 * coreMQTT over the FreeRTOS reference mbedTLS TLS transport
 * (component/network_transport/transport_mbedtls.c -> TLS_FreeRTOS_*) on
 * FreeRTOS-Plus-TCP. Mutual TLS to AWS IoT Core port 8883:
 *   - server trust  : Amazon Root CA 1 (embedded, aws_iot_config.h)
 *   - endpoint/cert/key : from TF-M PS (provisioned via `set ...`, encrypted at rest)
 *   - client id      : the module BLE MAC = AWS Thing name (efuse 0x190)
 *
 * Started from main()'s network-up hook (vApplicationIPNetworkEventHook). If the
 * device is not yet provisioned it idles and retries, so `set ...` can be run live.
 *
 * NOTE: this mbedTLS config has MBEDTLS_HAVE_TIME off, so X.509 validity-date checks
 * are skipped (no RTC wall-clock); the chain/signature to Amazon Root CA 1 is still
 * verified. Acceptable for the demo.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "core_mqtt.h"
#include "transport_mbedtls.h"      /* TLS_FreeRTOS_*, NetworkCredentials_t, TlsTransportParams_t */
#include "backoff_algorithm.h"

#include "aws_iot_config.h"
#include "aws_iot_creds.h"
#include "aws_provision.h"

extern int DiagPrintf(const char *fmt, ...);

/* mbedTLS FreeRTOS threading shim — must be registered once before any mbedTLS use
 * (component/mbedtls_config/mbedtls_threading_alt.c). It only calls mbedtls_threading_set_alt()
 * (function-pointer registration), so it is idempotent / safe to call repeatedly. */
extern void mbedtls_threading_freertos_init(void);

/* The FreeRTOS reference transport (transport_mbedtls.c, under MBEDTLS_THREADING_ALT) calls
 * mbedtls_platform_threading_init() inside initMbedtls() on every TLS_FreeRTOS_Connect. The
 * repo's threading port exposes mbedtls_threading_freertos_init() instead, so bridge them here. */
void mbedtls_platform_threading_init(void)
{
    mbedtls_threading_freertos_init();
}

/* coreMQTT transport context: the transport defines `struct NetworkContext` to hold a
 * TlsTransportParams_t*; the consumer mirrors that definition (C file-scope struct). */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

#define AWS_NET_BUFFER_SZ      2048
#define AWS_TLS_RECV_TMO_MS    3000
#define AWS_TLS_SEND_TMO_MS    3000
#define AWS_CONNECT_TMO_MS     8000

#define AWS_BACKOFF_BASE_MS    1000
#define AWS_BACKOFF_MAX_MS     16000
#define AWS_BACKOFF_MAX_TRIES  BACKOFF_ALGORITHM_RETRY_FOREVER

static uint8_t s_net_buffer[AWS_NET_BUFFER_SZ];

/* ----------------------------------------------------------------- time ----- */
static uint32_t prvGetTimeMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ------------------------------------------------------------- MQTT events --- */
static bool prvEventCallback(MQTTContext_t *pContext,
                             MQTTPacketInfo_t *pPacketInfo,
                             MQTTDeserializedInfo_t *pDeserializedInfo,
                             enum MQTTSuccessFailReasonCode *pReasonCode,
                             MQTTPropBuilder_t *pSendProps,
                             MQTTPropBuilder_t *pGetProps)
{
    (void)pContext; (void)pReasonCode; (void)pSendProps; (void)pGetProps;

    if ((pPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
        MQTTPublishInfo_t *pub = pDeserializedInfo->pPublishInfo;
        if (pub != NULL) {
            DiagPrintf("[AWS] RX publish topic=%.*s payload=%.*s\n\r",
                       (int)pub->topicNameLength, pub->pTopicName,
                       (int)pub->payloadLength, (const char *)pub->pPayload);
        }
    } else {
        DiagPrintf("[AWS] MQTT ctrl packet 0x%02x id=%u\n\r",
                   pPacketInfo->type, (unsigned)pDeserializedInfo->packetIdentifier);
    }
    return true;
}

/* ------------------------------------------------------- connect (TLS+MQTT) -- */
/* Returns 0 on a fully-established MQTT session, negative otherwise. */
static int prvConnect(MQTTContext_t *pMqtt, NetworkContext_t *pNet,
                      const char *endpoint, const char *clientId,
                      const NetworkCredentials_t *creds)
{
    TransportInterface_t transport;
    MQTTFixedBuffer_t fixedBuffer = { s_net_buffer, sizeof(s_net_buffer) };
    MQTTConnectInfo_t connectInfo;
    MQTTStatus_t st;
    TlsTransportStatus_t ts;
    bool sessionPresent = false;

    DiagPrintf("[AWS] connecting TLS to %s:%d ...\n\r", endpoint, AWS_MQTT_PORT);
    ts = TLS_FreeRTOS_Connect(pNet, endpoint, AWS_MQTT_PORT, creds,
                              AWS_TLS_RECV_TMO_MS, AWS_TLS_SEND_TMO_MS);
    if (ts != TLS_TRANSPORT_SUCCESS) {
        DiagPrintf("[AWS] TLS connect FAIL (%d)\n\r", (int)ts);
        return -1;
    }

    transport.pNetworkContext = pNet;
    transport.send = TLS_FreeRTOS_send;
    transport.recv = TLS_FreeRTOS_recv;
    transport.writev = NULL;

    st = MQTT_Init(pMqtt, &transport, prvGetTimeMs, prvEventCallback, &fixedBuffer);
    if (st != MQTTSuccess) {
        DiagPrintf("[AWS] MQTT_Init FAIL (%d)\n\r", (int)st);
        TLS_FreeRTOS_Disconnect(pNet);
        return -2;
    }

    memset(&connectInfo, 0, sizeof(connectInfo));
    connectInfo.cleanSession = true;
    connectInfo.keepAliveSeconds = AWS_MQTT_KEEPALIVE_S;
    connectInfo.pClientIdentifier = clientId;
    connectInfo.clientIdentifierLength = (uint16_t)strlen(clientId);
    /* AWS IoT mutual-TLS: no username/password (the X.509 client cert authenticates). */

    st = MQTT_Connect(pMqtt, &connectInfo, NULL, AWS_CONNECT_TMO_MS,
                      &sessionPresent, NULL, NULL);
    if (st != MQTTSuccess) {
        DiagPrintf("[AWS] MQTT_Connect FAIL (%d)\n\r", (int)st);
        TLS_FreeRTOS_Disconnect(pNet);
        return -3;
    }

    DiagPrintf("[AWS] MQTT connected to %s as '%s'\n\r", endpoint, clientId);
    return 0;
}

/* ------------------------------------------------------------- the task ----- */
static void aws_mqtt_task(void *param)
{
    MQTTContext_t mqtt;
    NetworkContext_t net;
    TlsTransportParams_t tlsParams;
    NetworkCredentials_t creds;
    char mac[16];
    char topic_tele[96], topic_cmd[96];
    /* Only the PRIVATE KEY comes from TF-M secure storage; the endpoint + client cert are
     * embedded (non-secret, aws_iot_creds.h). Static (.bss) key buffer, NOT pvPortMalloc:
     * psa_its_get writes into it across the NS->SPE boundary and the SPM whitelists NS SRAM
     * (.bss), not a PSRAM-backed heap. Only this task uses it. */
    static char key[AWS_KEY_MAX];
    size_t keyLen = 0;
    (void)param;

    net.pParams = &tlsParams;

    mbedtls_threading_freertos_init();

    if (aws_get_ble_mac(mac, sizeof(mac)) != 0) {
        DiagPrintf("[AWS] efuse BLE MAC read FAIL — task exiting\n\r");
        vTaskDelete(NULL);
        return;
    }
    DiagPrintf("[AWS] device (BLE MAC / Thing) = %s\n\r", mac);
    snprintf(topic_tele, sizeof(topic_tele), AWS_TOPIC_TELEMETRY_FMT, mac);
    snprintf(topic_cmd,  sizeof(topic_cmd),  AWS_TOPIC_CMD_FMT,  mac);

    /* Private key: prefer the one provisioned into TF-M secure storage (`set key`); fall back
     * to the embedded key so the demo connects out of the box.
     *
     * STATUS (2026-06-16): the runtime `set key` WRITE path now works — a full RSA-2048 key DER
     * stores into ITS and `set key: <n> b64 -> OK` is reported. (The earlier NS HardFault was
     * b64_store/its_store touching the KM0 LOG UART via DiagPrintf immediately after the secure
     * SPIC flash write, which takes an imprecise BusFault before the SPIC settles; fixed by
     * dropping the post-flash DiagPrintf and letting the FreeRTOS+CLI framework print the result.)
     *
     * REMAINING BLOCKER to the full chain: leaving a persistent key object in ITS makes the device
     * boot-HANG (~100% vs ~50% on a clean FS) — an intermittent ROM HardFault very early in the KM0
     * image2 XIP (PC in the 0x0C000000 window). This is the SPE<->KM0 cross-CPU flash-arbitration
     * gap (the SDK's FLASH_Write_Lock / IPC-sleep-KM0 is an IMAGE2-RAM/NS function the bare SPE
     * can't reach). Until that's solved the embedded fallback is what's used in practice. */
    {
        const char *pkey;
        if (aws_is_provisioned() && aws_load_key(key, AWS_KEY_MAX, &keyLen) == 0) {
            pkey = key;
            DiagPrintf("[AWS] private key from TF-M secure storage (%uB)\n\r", (unsigned)keyLen);
        } else {
            pkey = AWS_PRIVATE_KEY_PEM;
            keyLen = sizeof(AWS_PRIVATE_KEY_PEM);
            DiagPrintf("[AWS] private key embedded fallback (%uB) — `set key` not yet provisioned\n\r",
                       (unsigned)keyLen);
        }

        memset(&creds, 0, sizeof(creds));
        creds.pRootCa = (const uint8_t *)AWS_ROOT_CA_PEM;            /* embedded (aws_iot_config.h) */
        creds.rootCaSize = sizeof(AWS_ROOT_CA_PEM);                 /* literal: includes NUL */
        creds.pClientCert = (const uint8_t *)AWS_CLIENT_CERT_PEM;   /* embedded (aws_iot_creds.h) */
        creds.clientCertSize = sizeof(AWS_CLIENT_CERT_PEM);         /* literal: includes NUL */
        creds.pPrivateKey = (const uint8_t *)pkey;
        creds.privateKeySize = keyLen;                             /* includes NUL */
        creds.disableSni = 0;
    }

    DiagPrintf("[AWS] endpoint=%s cert=%uB\n\r",
               AWS_IOT_ENDPOINT, (unsigned)sizeof(AWS_CLIENT_CERT_PEM));

    /* Reconnect loop with exponential backoff. */
    for (;;) {
        BackoffAlgorithmContext_t backoff;
        BackoffAlgorithmStatus_t bs = BackoffAlgorithmSuccess;
        uint16_t nextBackoff = 0;

        BackoffAlgorithm_InitializeParams(&backoff, AWS_BACKOFF_BASE_MS,
                                          AWS_BACKOFF_MAX_MS, AWS_BACKOFF_MAX_TRIES);

        while (prvConnect(&mqtt, &net, AWS_IOT_ENDPOINT, mac, &creds) != 0) {
            uint32_t rnd = prvGetTimeMs();
            bs = BackoffAlgorithm_GetNextBackoff(&backoff, rnd, &nextBackoff);
            if (bs != BackoffAlgorithmSuccess) {
                DiagPrintf("[AWS] backoff exhausted — retrying in %u ms\n\r", AWS_BACKOFF_MAX_MS);
                nextBackoff = AWS_BACKOFF_MAX_MS;
            }
            DiagPrintf("[AWS] reconnect in %u ms\n\r", (unsigned)nextBackoff);
            vTaskDelay(pdMS_TO_TICKS(nextBackoff));
        }

        /* Subscribe to the command topic (QoS1). */
        {
            MQTTSubscribeInfo_t sub;
            memset(&sub, 0, sizeof(sub));
            sub.qos = MQTTQoS0;   /* QoS0: no MQTT_InitStatefulQoS state records needed */
            sub.pTopicFilter = topic_cmd;
            sub.topicFilterLength = (uint16_t)strlen(topic_cmd);
            if (MQTT_Subscribe(&mqtt, &sub, 1, MQTT_GetPacketId(&mqtt), NULL) != MQTTSuccess)
                DiagPrintf("[AWS] subscribe '%s' FAIL\n\r", topic_cmd);
            else
                DiagPrintf("[AWS] subscribed '%s'\n\r", topic_cmd);
        }

        /* Publish telemetry every AWS_TELEMETRY_PERIOD_MS; pump the loop in between. */
        {
            uint32_t seq = 0;
            MQTTStatus_t st = MQTTSuccess;
            TickType_t nextPub = xTaskGetTickCount();

            for (;;) {
                if (xTaskGetTickCount() >= nextPub) {
                    char payload[160];
                    MQTTPublishInfo_t pub;
                    int n = snprintf(payload, sizeof(payload),
                                     "{\"thing\":\"%s\",\"seq\":%u,\"uptime_ms\":%u,\"heap\":%u}",
                                     mac, (unsigned)seq,
                                     (unsigned)prvGetTimeMs(),
                                     (unsigned)xPortGetFreeHeapSize());
                    memset(&pub, 0, sizeof(pub));
                    pub.qos = MQTTQoS0;   /* QoS0: fire-and-forget telemetry, no QoS state records */
                    pub.pTopicName = topic_tele;
                    pub.topicNameLength = (uint16_t)strlen(topic_tele);
                    pub.pPayload = payload;
                    pub.payloadLength = (size_t)n;

                    st = MQTT_Publish(&mqtt, &pub, MQTT_GetPacketId(&mqtt), NULL);
                    if (st != MQTTSuccess) {
                        DiagPrintf("[AWS] publish FAIL (%d) — reconnecting\n\r", (int)st);
                        break;
                    }
                    DiagPrintf("[AWS] TX %s %s\n\r", topic_tele, payload);
                    seq++;
                    nextPub = xTaskGetTickCount() + pdMS_TO_TICKS(AWS_TELEMETRY_PERIOD_MS);
                }

                st = MQTT_ProcessLoop(&mqtt);
                if (st != MQTTSuccess && st != MQTTNeedMoreBytes) {
                    DiagPrintf("[AWS] ProcessLoop status %d — reconnecting\n\r", (int)st);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        MQTT_Disconnect(&mqtt, NULL, 0);
        TLS_FreeRTOS_Disconnect(&net);
        DiagPrintf("[AWS] disconnected; will reconnect\n\r");
        vTaskDelay(pdMS_TO_TICKS(AWS_BACKOFF_BASE_MS));
    }
}

void aws_iot_demo_start(void)
{
    /* 8K-word (32 KB) stack: enough for the mbedTLS handshake, while leaving the shared
     * FreeRTOS heap room for the per-connection TLS record buffers (a 64 KB stack starved the
     * heap -> vApplicationMallocFailedHook during the handshake). */
    if (xTaskCreate(aws_mqtt_task, "aws_mqtt", 8192, NULL,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS)
        DiagPrintf("[AWS] xTaskCreate(aws_mqtt) FAILED\n\r");
}
