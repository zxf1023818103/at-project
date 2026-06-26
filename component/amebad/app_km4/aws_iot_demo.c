/*
 * aws_iot_demo.c - AmebaD KM4 AWS IoT Core MQTT/TLS client over coreMQTT-Agent
 * (make AWS=ON).
 *
 * Refactored from the direct-coreMQTT telemetry loop to the coreMQTT-Agent so the
 * telemetry task and the OTA agent task safely SHARE one MQTT connection:
 *   - aws_agent_task    owns the TLS socket + the MQTTAgentContext_t. It connects
 *                       (TLS + MQTT_Connect), then runs MQTTAgent_CommandLoop(), which
 *                       serialises every other task's MQTT API calls and pumps
 *                       MQTT_ProcessLoop. On disconnect it reconnects (backoff) +
 *                       MQTTAgent_ResumeSession and loops again.
 *   - aws_telemetry_task waits for the agent to be connected, subscribes to the command
 *                       topic (via the subscription_manager), then publishes telemetry
 *                       JSON to bw20/<mac>/telemetry every period using MQTTAgent_Publish.
 *   - the OTA agent     (ota_app.c) shares the same MQTTAgentContext_t for its job /
 *                       stream topics; ota_app_init() is started after the first connect.
 *
 * Mutual TLS to AWS IoT Core port 8883 (server trust = Amazon Root CA 1; endpoint = env
 * "endpoint"; client cert = plain LFS DER via "cert" env; private key = TF-M ITS). Thing
 * name = BLE MAC (efuse 0x190) = MQTT client id. There is no embedded client identity;
 * an unprovisioned device idle-retries so `set ...` can run live.
 */
#include <stdint.h>
#include "at_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "transport_tls.h"
#include "backoff_algorithm.h"

#include "aws_iot_config.h"
#include "aws_provision.h"

#include "freertos_agent_message.h"
#include "freertos_command_pool.h"
#include "subscription_manager.h"
#include "ota_app.h"

#ifndef AT_FW_VERSION
#define AT_FW_VERSION "dev"
#endif

extern void mbedtls_threading_freertos_init(void);

/* transport_mbedtls.c calls mbedtls_platform_threading_init() inside its connect path;
 * bridge it to the repo's threading port (idempotent function-pointer registration). */
void mbedtls_platform_threading_init(void)
{
    mbedtls_threading_freertos_init();
}

struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

#define AWS_NET_BUFFER_SZ      2048   /* coreMQTT-Agent fixed buffer: must hold the largest
                                        * packet pumped through the shared connection - OTA
                                        * job docs / 4 KB stream data blocks dwarf the tiny
                                        * telemetry PUBLISH, so this is bumped from the old
                                        * 1 KB telemetry-only buffer. */
#define AWS_TLS_RECV_TMO_MS    1000
#define AWS_TLS_SEND_TMO_MS    1000
#define AWS_CONNECT_TMO_MS     8000

#define AWS_BACKOFF_BASE_MS    1000
#define AWS_BACKOFF_MAX_MS     16000
#define AWS_BACKOFF_MAX_TRIES  BACKOFF_ALGORITHM_RETRY_FOREVER

/* MQTT v5 ACK-properties buffer required by MQTTAgent_Init (QoS1/2 ack props). */
#define AWS_ACK_PROPS_BUF_SZ   256

static uint8_t s_net_buffer[AWS_NET_BUFFER_SZ];
static uint8_t s_ack_props_buffer[AWS_ACK_PROPS_BUF_SZ];

/* The shared MQTT-Agent context + its message-queue backend (command pointers). */
static MQTTAgentContext_t      s_agent;
static MQTTAgentMessageContext_t s_agentMsgCtx;     /* { QueueHandle_t queue } - see freertos_agent_message.h */
#define AWS_AGENT_QUEUE_LEN    ( 25 )
static StaticQueue_t           s_agentQueueStorage;
static uint8_t                 s_agentQueueBuf[ AWS_AGENT_QUEUE_LEN * sizeof( MQTTAgentCommand_t * ) ];

/* Connection bits shared between the agent task and the telemetry task. */
static NetworkContext_t        s_net;
static TlsTransportParams_t    s_tlsParams;
static NetworkCredentials_t    s_creds;
static char                    s_mac[16];
static char                    s_endpoint[AWS_ENDPOINT_MAX];
static char                    s_certPath[64];
static char                    s_key[AWS_KEY_MAX];
static size_t                  s_keyLen = 0;
static char                    s_topic_tele[96];
static char                    s_topic_cmd[96];

static volatile int            s_connected = 0;   /* set by the agent task once MQTT is up */
static volatile int            s_otaStarted = 0;

/* ----------------------------------------------------------------- time ----- */
static uint32_t prvGetTimeMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* --------------------------------------------------- incoming publish route -- */
/* The agent's single incoming-publish callback: fan out via the subscription manager
 * (the OTA topics + the command topic each register a handler there). */
static void prvIncomingPublishCallback(MQTTAgentContext_t *pAgentCtx,
                                       uint16_t packetId,
                                       MQTTPublishInfo_t *pPublishInfo)
{
    (void)pAgentCtx; (void)packetId;
    if (SubscriptionManager_HandleIncomingPublishes(pPublishInfo) == false) {
        log_info("[AWS] RX unrouted publish topic=%.*s",
                 (int)pPublishInfo->topicNameLength, pPublishInfo->pTopicName);
    }
}

/* The command topic handler (replaces the old prvEventCallback PUBLISH branch). */
static void prvCommandTopicHandler(void *pvCtx, MQTTPublishInfo_t *pPublishInfo)
{
    (void)pvCtx;
    log_info("[AWS] RX publish topic=%.*s payload=%.*s",
             (int)pPublishInfo->topicNameLength, pPublishInfo->pTopicName,
             (int)pPublishInfo->payloadLength, (const char *)pPublishInfo->pPayload);
}

/* ----------------------------------------------- provisioning (idle-retry) --- */
static void prvWaitForProvisioning(void)
{
    for (;;) {
        const char *ep = getenv("endpoint");
        const char *cp = getenv("cert");
        int rk = aws_load_key(s_key, sizeof(s_key), &s_keyLen);
        if (ep != NULL && cp != NULL && rk == 0) {
            strncpy(s_endpoint, ep, sizeof(s_endpoint) - 1);
            s_endpoint[sizeof(s_endpoint) - 1] = '\0';
            strncpy(s_certPath, cp, sizeof(s_certPath) - 1);
            s_certPath[sizeof(s_certPath) - 1] = '\0';
            break;
        }
        log_info("[AWS] NOT provisioned (endpoint:%s cert:%s key:%s, free heap %u) - run: set endpoint <ep> / set cert <b64> / set key <b64>",
                 ep ? "ok" : "MISSING", cp ? "ok" : "MISSING", rk ? "MISSING" : "ok",
                 (unsigned)xPortGetFreeHeapSize());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    log_info("[AWS] provisioned: endpoint=%s certPath=%s key=%uB (free heap %u)",
             s_endpoint, s_certPath, (unsigned)s_keyLen, (unsigned)xPortGetFreeHeapSize());
}

/* ----------------------------------------- TLS connect + MQTT_Connect (agent) */
/* Establish the TLS socket and the MQTT session on the agent's own mqttContext.
 * Returns 0 on a fully-established MQTT session, negative otherwise. */
static int prvAgentConnect(bool *pSessionPresent)
{
    TransportInterface_t transport;
    MQTTConnectInfo_t connectInfo;
    MQTTStatus_t st;
    TlsTransportStatus_t ts;

    log_info("[AWS] connecting TLS to %s:%d ...", s_endpoint, AWS_MQTT_PORT);
    ts = TLS_FreeRTOS_Connect(&s_net, s_endpoint, AWS_MQTT_PORT, &s_creds,
                              AWS_TLS_RECV_TMO_MS, AWS_TLS_SEND_TMO_MS);
    if (ts != TLS_TRANSPORT_SUCCESS) {
        log_error("[AWS] TLS connect FAIL (%d)", (int)ts);
        return -1;
    }

    transport.pNetworkContext = &s_net;
    transport.send = TLS_FreeRTOS_send;
    transport.recv = TLS_FreeRTOS_recv;
    transport.writev = NULL;

    /* (Re)initialise the agent over this transport: MQTTAgent_Init does MQTT_Init +
     * MQTT_InitStatefulQoS internally, using the records embedded in s_agent. */
    {
        MQTTFixedBuffer_t fixedBuffer = { s_net_buffer, sizeof(s_net_buffer) };
        MQTTAgentMessageInterface_t messageInterface;

        messageInterface.pMsgCtx        = &s_agentMsgCtx;
        messageInterface.send           = Agent_MessageSend;
        messageInterface.recv           = Agent_MessageReceive;
        messageInterface.getCommand     = Agent_GetCommand;
        messageInterface.releaseCommand = Agent_ReleaseCommand;

        st = MQTTAgent_Init(&s_agent, &messageInterface, &fixedBuffer, &transport,
                            prvGetTimeMs, prvIncomingPublishCallback, NULL,
                            s_ack_props_buffer, sizeof(s_ack_props_buffer));
        if (st != MQTTSuccess) {
            log_error("[AWS] MQTTAgent_Init FAIL (%d)", (int)st);
            TLS_FreeRTOS_Disconnect(&s_net);
            return -2;
        }
    }

    memset(&connectInfo, 0, sizeof(connectInfo));
    connectInfo.cleanSession = true;
    connectInfo.keepAliveSeconds = AWS_MQTT_KEEPALIVE_S;
    connectInfo.pClientIdentifier = s_mac;
    connectInfo.clientIdentifierLength = (uint16_t)strlen(s_mac);

    st = MQTT_Connect(&s_agent.mqttContext, &connectInfo, NULL, AWS_CONNECT_TMO_MS,
                      pSessionPresent, NULL, NULL);
    if (st != MQTTSuccess) {
        log_error("[AWS] MQTT_Connect FAIL (%d)", (int)st);
        TLS_FreeRTOS_Disconnect(&s_net);
        return -3;
    }

    log_info("[AWS] MQTT connected to %s as '%s'", s_endpoint, s_mac);
    return 0;
}

/* ------------------------------------------------------- the agent task ----- */
static void aws_agent_task(void *param)
{
    (void)param;

    s_net.pParams = &s_tlsParams;
    mbedtls_threading_freertos_init();

    if (aws_get_ble_mac(s_mac, sizeof(s_mac)) != 0) {
        log_error("[AWS] efuse BLE MAC read FAIL - task exiting");
        vTaskDelete(NULL);
        return;
    }
    log_info("[AWS] fw version = %s", AT_FW_VERSION);
    log_info("[AWS] device (BLE MAC / Thing) = %s", s_mac);
    snprintf(s_topic_tele, sizeof(s_topic_tele), AWS_TOPIC_TELEMETRY_FMT, s_mac);
    snprintf(s_topic_cmd,  sizeof(s_topic_cmd),  AWS_TOPIC_CMD_FMT,  s_mac);

    prvWaitForProvisioning();

    memset(&s_creds, 0, sizeof(s_creds));
    s_creds.pRootCa = (const uint8_t *)AWS_ROOT_CA_PEM;
    s_creds.rootCaSize = sizeof(AWS_ROOT_CA_PEM);
    s_creds.pClientCert = NULL;
    s_creds.clientCertSize = 0;
    s_creds.pClientCertPath = s_certPath;
    s_creds.pPrivateKey = (const uint8_t *)s_key;
    s_creds.privateKeySize = s_keyLen;
    s_creds.disableSni = 0;
    /* MQTT here only carries small messages (telemetry, OTA job docs <1 KB), so an 8 KB picotls
     * rxPlain reserve is ample - it leaves ~8 KB more heap for the OTA HTTP data plane's 2nd TLS
     * (the 16 KB default is only needed for bulk TLS reads, which the HTTPS download owns instead). */
    s_creds.rxReserveSize = 6144;

    /* The command-pointer queue the agent + app tasks rendezvous on. */
    s_agentMsgCtx.queue = xQueueCreateStatic(AWS_AGENT_QUEUE_LEN,
                                             sizeof(MQTTAgentCommand_t *),
                                             s_agentQueueBuf, &s_agentQueueStorage);
    configASSERT(s_agentMsgCtx.queue != NULL);

    for (;;) {
        BackoffAlgorithmContext_t backoff;
        BackoffAlgorithmStatus_t bs = BackoffAlgorithmSuccess;
        uint16_t nextBackoff = 0;
        bool sessionPresent = false;
        MQTTStatus_t loopStatus;

        BackoffAlgorithm_InitializeParams(&backoff, AWS_BACKOFF_BASE_MS,
                                          AWS_BACKOFF_MAX_MS, AWS_BACKOFF_MAX_TRIES);

        while (prvAgentConnect(&sessionPresent) != 0) {
            uint32_t rnd = prvGetTimeMs();
            bs = BackoffAlgorithm_GetNextBackoff(&backoff, rnd, &nextBackoff);
            if (bs != BackoffAlgorithmSuccess) {
                nextBackoff = AWS_BACKOFF_MAX_MS;
            }
            log_info("[AWS] reconnect in %u ms", (unsigned)nextBackoff);
            vTaskDelay(pdMS_TO_TICKS(nextBackoff));
        }

        /* Resume any QoS1/2 state if the broker kept the session (it won't with
         * cleanSession=true, but ResumeSession also clears stale state cleanly). */
        (void)MQTTAgent_ResumeSession(&s_agent, sessionPresent);

        s_connected = 1;

        /* Start the OTA agent on the first connect; on later reconnects just re-kick it.
         * (The OTA agent re-subscribes its own job/stream topics through the agent.) */
        if (!s_otaStarted) {
            if (ota_app_init(&s_agent, s_mac) == 0) {
                s_otaStarted = 1;
            }
        }
        if (s_otaStarted) {
            ota_app_on_mqtt_connected();
        }

        /* Run the command loop - this owns the socket + pumps ProcessLoop until a
         * disconnect or error, serialising telemetry/OTA MQTT commands. */
        loopStatus = MQTTAgent_CommandLoop(&s_agent);

        /* Loop returned: connection is down. Tear down + reconnect. */
        s_connected = 0;
        log_info("[AWS] command loop exited (%d) - disconnecting", (int)loopStatus);
        TLS_FreeRTOS_Disconnect(&s_net);

        /* Cancel any commands queued while the loop was not running so their
         * completion callbacks fire (waking blocked telemetry/OTA wrappers). */
        (void)MQTTAgent_CancelAll(&s_agent);

        vTaskDelay(pdMS_TO_TICKS(AWS_BACKOFF_BASE_MS));
    }
}

/* ----------------------------------------------------- the telemetry task --- */
static void prvSubscribeCmdComplete(MQTTAgentCommandContext_t *pCmdCtx,
                                    MQTTAgentReturnInfo_t *pReturnInfo)
{
    TaskHandle_t task = (TaskHandle_t)pCmdCtx;
    (void)pReturnInfo;
    if (task != NULL) {
        xTaskNotifyGive(task);
    }
}

static void prvSubscribeCommandTopic(void)
{
    MQTTAgentSubscribeArgs_t subscribeArgs;
    MQTTSubscribeInfo_t subscribeInfo;
    MQTTAgentCommandInfo_t commandInfo = { 0 };

    /* Register the routing entry first, then enqueue the SUBSCRIBE. */
    if (SubscriptionManager_AddSubscription(s_topic_cmd, (uint16_t)strlen(s_topic_cmd),
                                            prvCommandTopicHandler, NULL) == false) {
        log_error("[AWS] subscription table full for '%s'", s_topic_cmd);
        return;
    }

    memset(&subscribeInfo, 0, sizeof(subscribeInfo));
    subscribeInfo.qos = MQTTQoS0;
    subscribeInfo.pTopicFilter = s_topic_cmd;
    subscribeInfo.topicFilterLength = (uint16_t)strlen(s_topic_cmd);
    subscribeArgs.pSubscribeInfo = &subscribeInfo;
    subscribeArgs.numSubscriptions = 1U;
    subscribeArgs.pProperties = NULL;

    commandInfo.cmdCompleteCallback = prvSubscribeCmdComplete;
    commandInfo.pCmdCompleteCallbackContext = (MQTTAgentCommandContext_t *)xTaskGetCurrentTaskHandle();
    commandInfo.blockTimeMs = 5000;

    if (MQTTAgent_Subscribe(&s_agent, &subscribeArgs, &commandInfo) != MQTTSuccess) {
        log_error("[AWS] subscribe enqueue '%s' FAIL", s_topic_cmd);
        SubscriptionManager_RemoveSubscription(s_topic_cmd, (uint16_t)strlen(s_topic_cmd));
        return;
    }
    /* Wait (best-effort) for the SUBACK so an early command is not missed. */
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) != 0) {
        log_info("[AWS] subscribed '%s'", s_topic_cmd);
    } else {
        log_info("[AWS] subscribe '%s' (no SUBACK within 5s)", s_topic_cmd);
    }
}

static void aws_telemetry_task(void *param)
{
    uint32_t seq = 0;
    int wasConnected = 0;
    (void)param;

    for (;;) {
        if (!s_connected) {
            wasConnected = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Pause telemetry while an OTA download is in progress: the data plane opens a 2nd TLS
         * (HTTPS to S3) whose handshake needs a large contiguous heap block, and the periodic
         * telemetry publish churn fragments the heap (intermittent handshake recv -6). Stay quiet
         * until the OTA is back to idle. */
        if (ota_app_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* On a fresh connection, (re)subscribe the command topic. */
        if (!wasConnected) {
            prvSubscribeCommandTopic();
            wasConnected = 1;
        }

        {
            char payload[160];
            MQTTPublishInfo_t pub;
            MQTTAgentPublishArgs_t pubArgs;
            MQTTAgentCommandInfo_t commandInfo = { 0 };
            int n = snprintf(payload, sizeof(payload),
                             "{\"thing\":\"%s\",\"fw\":\"%s\",\"seq\":%u,\"uptime_ms\":%u,\"heap\":%u}",
                             s_mac, AT_FW_VERSION, (unsigned)seq,
                             (unsigned)prvGetTimeMs(),
                             (unsigned)xPortGetFreeHeapSize());

            memset(&pub, 0, sizeof(pub));
            pub.qos = MQTTQoS0;
            pub.pTopicName = s_topic_tele;
            pub.topicNameLength = (uint16_t)strlen(s_topic_tele);
            pub.pPayload = payload;
            pub.payloadLength = (size_t)n;
            pubArgs.pPublishInfo = &pub;
            pubArgs.pProperties = NULL;

            /* Even for QoS0 we BLOCK on completion: the publishInfo + payload[] live on this
             * task's stack and MUST stay in scope until the agent has serialised the publish
             * (the agent only copies into the network buffer when it processes the command).
             * The completion callback notifies us; cmdCtx is this task's handle. */
            commandInfo.cmdCompleteCallback = prvSubscribeCmdComplete; /* generic notify-on-complete */
            commandInfo.pCmdCompleteCallbackContext = (MQTTAgentCommandContext_t *)xTaskGetCurrentTaskHandle();
            commandInfo.blockTimeMs = 1000;

            if (MQTTAgent_Publish(&s_agent, &pubArgs, &commandInfo) == MQTTSuccess) {
                /* Wait for the agent to consume the command (publish serialised). */
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) != 0) {
                    log_info("[AWS] TX %s %s", s_topic_tele, payload);
                    seq++;
                } else {
                    log_error("[AWS] publish '%s' no completion within 5s", s_topic_tele);
                }
            } else {
                log_error("[AWS] publish enqueue FAIL");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(AWS_TELEMETRY_PERIOD_MS));
    }
}

void aws_iot_demo_start(void)
{
    /* The OTA close-file path (mbedTLS x509 parse + ECDSA verify) and the TLS handshake
     * are the heavy stack users; 8K words for the agent task. The telemetry task is light. */
    /* 5120 words (20 KB), not 8192/32 KB: this task runs MQTTAgent_CommandLoop + the one-shot TLS
     * handshake (mbedTLS RSA-2048 cert-chain verify peaks ~12 KB of stack), so 32 KB was ~2.6x over.
     * Reclaiming 12 KB of heap is what lets the OTA HTTPS data plane open its 2nd concurrent TLS. */
    if (xTaskCreate(aws_agent_task, "aws_agent", 5120, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        log_error("[AWS] xTaskCreate(aws_agent) FAILED");

    if (xTaskCreate(aws_telemetry_task, "aws_tele", 2048, NULL,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS)
        log_error("[AWS] xTaskCreate(aws_tele) FAILED");
}
