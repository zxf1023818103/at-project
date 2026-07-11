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
#include "aws_iot.h"        /* shared AWS component: aws_load_key / aws_get_mac / AWS_*_MAX */
#include "aws_connect.h"    /* shared TLS+MQTT connect handshake (aws_connect_tls) */
#include "aws_telemetry_common.h" /* shared topic (bw20/) + JSON schema, same as aws_telemetry.c */

#include "freertos_agent_message.h"
#include "freertos_command_pool.h"
#include "subscription_manager.h"
#include "ota_app.h"

#if defined(MBEDTLS_GCM_ALT)
#include "mbedtls/gcm.h"    /* HW AES-GCM (gcm_alt.c) boot self-test, below */
#endif

/* AT_FW_VERSION fallback now lives in aws_telemetry_common.h (shared telemetry schema). */

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

#ifndef AWS_NET_BUFFER_SZ
#define AWS_NET_BUFFER_SZ      4736   /* MQTT data plane: hold a 4 KB OTA block PUBLISH. largest
                                        * packet pumped through the shared connection - OTA
                                        * job docs / 4 KB stream data blocks dwarf the tiny
                                        * telemetry PUBLISH, so this is bumped from the old
                                        * 1 KB telemetry-only buffer. bl602 overrides to 2560
                                        * (2 KB blocks) via -D to save heap. */
#endif
#define AWS_TLS_RECV_TMO_MS    1000
#define AWS_TLS_SEND_TMO_MS    1000
#define AWS_CONNECT_TMO_MS     8000

#define AWS_BACKOFF_BASE_MS    1000
#define AWS_BACKOFF_MAX_MS     16000
#define AWS_BACKOFF_MAX_TRIES  BACKOFF_ALGORITHM_RETRY_FOREVER

/* MQTT v5 ACK-properties buffer required by MQTTAgent_Init (QoS1/2 ack props). */
#define AWS_ACK_PROPS_BUF_SZ   256

/* --- platform-tunable knobs (defaults = AmebaD; bl602 overrides via -D in its CMake) ------------ *
 * AmebaD forces picotls (TLS 1.3) + a 6 KB rx reserve so the OTA HTTPS 2nd data-plane TLS has heap
 * headroom, and trusts the LittleFS `ca/` FOLDER. bl602 runs the MQTT data plane (single TLS, no 2nd
 * connection) so it uses the default TLS backend, no rx reserve, and one provisioned CA file (env
 * "ca"). The OTA data plane itself is selected by OTA_APP_ENABLE_HTTP in ota_app.c (AmebaD 1, bl602 0). */
#ifndef AWS_TLS_RX_RESERVE
#define AWS_TLS_RX_RESERVE     6144
#endif
#ifndef AWS_TLS_BACKEND_MODE
#define AWS_TLS_BACKEND_MODE   TLS_BACKEND_MODE_PICOTLS
#endif
#ifndef AWS_AGENT_STACK_WORDS
#define AWS_AGENT_STACK_WORDS  5120   /* MQTTAgent_CommandLoop + the one-shot mbedTLS handshake (~12 KB peak) */
#endif
#ifndef AWS_TELE_STACK_WORDS
#define AWS_TELE_STACK_WORDS   1024
#endif

/* Server-trust path: AmebaD uses the `ca/` folder shipped in the image; bl602 (AWS_CA_USE_ENV) uses
 * the CA file provisioned via `aws ca` (env "ca"), falling back to the aws_iot.h default file. */
static const char * prvCaPath( void )
{
#ifdef AWS_CA_USE_ENV
    const char * e = getenv( "ca" );
    if( ( e != NULL ) && ( e[ 0 ] != '\0' ) )
    {
        return e;
    }
    return AWS_CA_PATH;
#else
    return "ca";
#endif
}

#ifdef AWS_NET_BUFFER_DYNAMIC
/* bl602: malloc'd lazily AFTER the TLS handshake (see aws_iot_demo_start) so its 4.7 KB isn't held
 * in .bss (which permanently shrinks the TLSF heap) during the heap-tight server-cert-chain verify.
 * The BL602 HW PKA can't correctly modexp a 2048-bit modulus, so RSA-2048 signature verification
 * falls back to the software mbedtls_mpi_exp_mod (~5 KB heap); the AWS cert flight otherwise leaves
 * only ~4.7 KB free at that point. Freeing this buffer from .bss gives the modexp room. */
static uint8_t *s_net_buffer = NULL;
#else
static uint8_t s_net_buffer[AWS_NET_BUFFER_SZ];
#endif
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
        log_info("[AWS] NOT provisioned (endpoint:%s cert:%s key:%s, free heap %u) - run: aws endpoint <ep> / aws cert <b64> / aws key <b64>",
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
    aws_connect_params_t p;

    /* Shared handshake: NetworkCredentials fill + TLS_FreeRTOS_Connect + transport/connectInfo fill.
     * AmebaD forces picotls (TLS 1.3) + a 6 KB rx reserve (leaving heap for the OTA HTTP 2nd TLS), and
     * the CA is the LittleFS `ca/` FOLDER (augments the Mozilla bundle in a CRT_BUNDLE build). */
    memset(&p, 0, sizeof(p));
    p.endpoint = s_endpoint;  p.certPath = s_certPath;  p.caPath = prvCaPath();
    p.key = (const uint8_t *)s_key;  p.keyLen = s_keyLen;  p.clientId = s_mac;
    p.port = AWS_MQTT_PORT;  p.recvTmoMs = AWS_TLS_RECV_TMO_MS;  p.sendTmoMs = AWS_TLS_SEND_TMO_MS;
    p.keepAliveS = AWS_MQTT_KEEPALIVE_S;  p.rxReserveSize = AWS_TLS_RX_RESERVE;  p.backendMode = AWS_TLS_BACKEND_MODE;
    log_info("[AWS] pre-handshake free heap %u min-ever %u", (unsigned)xPortGetFreeHeapSize(),
             (unsigned)xPortGetMinimumEverFreeHeapSize());
    if (aws_connect_tls(&s_net, &s_creds, &transport, &connectInfo, &p) != 0) {
        return -1;   /* aws_connect_tls logged the failure + tore down the socket */
    }

#ifdef AWS_NET_BUFFER_DYNAMIC
    /* Now that the (heap-tight) TLS handshake is done, claim the MQTT data-plane buffer. Malloc'd
     * once and reused across reconnects. Keeping it out of .bss above gave the handshake its heap. */
    if (s_net_buffer == NULL) {
        s_net_buffer = (uint8_t *)pvPortMalloc(AWS_NET_BUFFER_SZ);
        if (s_net_buffer == NULL) {
            log_error("[AWS] MQTT net buffer alloc FAIL (%u B)", (unsigned)AWS_NET_BUFFER_SZ);
            TLS_FreeRTOS_Disconnect(&s_net);
            return -1;
        }
    }
#endif

    /* (Re)initialise the agent over this transport: MQTTAgent_Init does MQTT_Init +
     * MQTT_InitStatefulQoS internally, using the records embedded in s_agent. */
    {
        MQTTFixedBuffer_t fixedBuffer = { s_net_buffer, AWS_NET_BUFFER_SZ };
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

#if defined(MBEDTLS_GCM_ALT)
/* Boot self-test for the HW AES-GCM backend (MBEDTLS_GCM_ALT): bl602 = gcm_alt.c HW AES-CTR + software
 * GHASH; AmebaD = the RTL8721D IPSEC ROM engine (component/amebad/mbedtls_port/gcm_alt.c). Runs once
 * before the first TLS handshake so a HW-crypto miscompute (wrong tag / bad multi-block CTR) shows up
 * as a clear [HWGCM] FAIL instead of an opaque AEAD/handshake failure. Two checks:
 *   1. NIST GCM Test Case 4 known-answer (AES-128, 60 B PT, 20 B AAD) -- exact ciphertext + tag.
 *   2. A 1024 B encrypt->auth_decrypt round-trip -- exercises the multi-block/multi-chunk CTR path. */
static const uint8_t k_gcm_key[16] = { 0xfe,0xff,0xe9,0x92,0x86,0x65,0x73,0x1c,0x6d,0x6a,0x8f,0x94,0x67,0x30,0x83,0x08 };
static const uint8_t k_gcm_iv[12]  = { 0xca,0xfe,0xba,0xbe,0xfa,0xce,0xdb,0xad,0xde,0xca,0xf8,0x88 };
static const uint8_t k_gcm_aad[20] = { 0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xfe,0xed,0xfa,0xce,0xde,0xad,0xbe,0xef,0xab,0xad,0xda,0xd2 };
static const uint8_t k_gcm_pt[60]  = {
    0xd9,0x31,0x32,0x25,0xf8,0x84,0x06,0xe5,0xa5,0x59,0x09,0xc5,0xaf,0xf5,0x26,0x9a,
    0x86,0xa7,0xa9,0x53,0x15,0x34,0xf7,0xda,0x2e,0x4c,0x30,0x3d,0x8a,0x31,0x8a,0x72,
    0x1c,0x3c,0x0c,0x95,0x95,0x68,0x09,0x53,0x2f,0xcf,0x0e,0x24,0x49,0xa6,0xb5,0x25,
    0xb1,0x6a,0xed,0xf5,0xaa,0x0d,0xe6,0x57,0xba,0x63,0x7b,0x39 };
static const uint8_t k_gcm_ct[60]  = {
    0x42,0x83,0x1e,0xc2,0x21,0x77,0x74,0x24,0x4b,0x72,0x21,0xb7,0x84,0xd0,0xd4,0x9c,
    0xe3,0xaa,0x21,0x2f,0x2c,0x02,0xa4,0xe0,0x35,0xc1,0x7e,0x23,0x29,0xac,0xa1,0x2e,
    0x21,0xd5,0x14,0xb2,0x54,0x66,0x93,0x1c,0x7d,0x8f,0x6a,0x5a,0xac,0x84,0xaa,0x05,
    0x1b,0xa3,0x0b,0x39,0x6a,0x0a,0xac,0x97,0x3d,0x58,0xe0,0x91 };
static const uint8_t k_gcm_tag[16] = { 0x5b,0xc9,0x4f,0xbc,0x32,0x21,0xa5,0xdb,0x94,0xfa,0xe9,0x5a,0xe7,0x12,0x1a,0x47 };

#define HWGCM_KAT_BIG 1024u    /* > the 512 B CTR chunk -> exercises multi-chunk + multi-block CTR */

static void bl602_hwgcm_selftest(void)
{
    uint8_t out[60], tag[16];
    /* Stage the operands the HW engine reads into SRAM: the AmebaD RTL8721D IPSEC GCM engine DMAs its
     * inputs and CANNOT read XIP flash, but the vectors are `static const` (.rodata -> flash). Without
     * this the engine reads garbage and TC4 spuriously FAILs while the block-aligned self-consistent
     * roundtrip still passes -- the exact symptom seen on AmebaD. (bl602's gcm_alt stages into its own
     * NOCACHE scratch, so these copies are a no-op there.) Expected CT/tag/PT stay in flash: they are
     * CPU memcmp'd, not DMA'd. */
    uint8_t iv_s[12], aad_s[20], pt_s[60], ct_s[60];
    mbedtls_gcm_context gcm;
    int tc4_enc = 0, tc4_dec = 0, big_rt = -1, i;
    uint8_t *pt = NULL, *ct = NULL, *dec = NULL;

    memcpy(iv_s, k_gcm_iv, 12);  memcpy(aad_s, k_gcm_aad, 20);
    memcpy(pt_s, k_gcm_pt, 60);  memcpy(ct_s, k_gcm_ct, 60);

    /* 1. NIST TC4 known-answer (encrypt: exact CT + tag; decrypt: recover PT + accept tag). */
    mbedtls_gcm_init(&gcm);
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, k_gcm_key, 128) == 0 &&
        mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, 60, iv_s, 12,
                                  aad_s, 20, pt_s, out, 16, tag) == 0 &&
        memcmp(out, k_gcm_ct, 60) == 0 && memcmp(tag, k_gcm_tag, 16) == 0) {
        tc4_enc = 1;
    }
    if (mbedtls_gcm_auth_decrypt(&gcm, 60, iv_s, 12, aad_s, 20,
                                 k_gcm_tag, 16, ct_s, out) == 0 &&
        memcmp(out, k_gcm_pt, 60) == 0) {
        tc4_dec = 1;
    }
    mbedtls_gcm_free(&gcm);

    /* 2. 1024 B round-trip -- multi-block CTR (auto-increment) + multi-chunk (>512 B) path. Buffers
     *    are heap (transient, freed here) so nothing permanent is taken from the tight handshake heap. */
    pt  = pvPortMalloc(HWGCM_KAT_BIG);
    ct  = pvPortMalloc(HWGCM_KAT_BIG);
    dec = pvPortMalloc(HWGCM_KAT_BIG);
    if (pt != NULL && ct != NULL && dec != NULL) {
        big_rt = 0;
        for (i = 0; i < (int)HWGCM_KAT_BIG; i++) { pt[i] = (uint8_t)(i * 7 + 3); }
        mbedtls_gcm_init(&gcm);
        if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, k_gcm_key, 128) == 0 &&
            mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, HWGCM_KAT_BIG, iv_s, 12,
                                      aad_s, 20, pt, ct, 16, tag) == 0 &&
            mbedtls_gcm_auth_decrypt(&gcm, HWGCM_KAT_BIG, iv_s, 12, aad_s, 20,
                                     tag, 16, ct, dec) == 0 &&     /* separate output (as picotls) */
            memcmp(dec, pt, HWGCM_KAT_BIG) == 0) {
            big_rt = 1;
        }
        mbedtls_gcm_free(&gcm);
    }
    if (pt  != NULL) { vPortFree(pt); }
    if (ct  != NULL) { vPortFree(ct); }
    if (dec != NULL) { vPortFree(dec); }

    log_warn("[HWGCM] self-test: TC4-enc=%s TC4-dec=%s 1024B-roundtrip=%s -> %s",
             tc4_enc ? "PASS" : "FAIL", tc4_dec ? "PASS" : "FAIL",
             (big_rt < 0) ? "SKIP(oom)" : (big_rt ? "PASS" : "FAIL"),
             (tc4_enc && tc4_dec && big_rt != 0) ? "HW AES-GCM OK" : "HW AES-GCM BROKEN");
}
#endif /* MBEDTLS_GCM_ALT */

/* ------------------------------------------------------- the agent task ----- */
static void aws_agent_task(void *param)
{
    (void)param;

    s_net.pParams = &s_tlsParams;
    mbedtls_threading_freertos_init();

#if defined(MBEDTLS_GCM_ALT)
    bl602_hwgcm_selftest();     /* validate the HW AES-GCM before the first TLS handshake uses it */
#endif

    if (aws_get_mac(s_mac, sizeof(s_mac)) != 0) {
        log_error("[AWS] efuse BLE MAC read FAIL - task exiting");
        vTaskDelete(NULL);
        return;
    }
    log_info("[AWS] fw version = %s", AT_FW_VERSION);
    log_info("[AWS] device (BLE MAC / Thing) = %s", s_mac);
    snprintf(s_topic_tele, sizeof(s_topic_tele), AWS_TOPIC_TELEMETRY_FMT, s_mac);
    snprintf(s_topic_cmd,  sizeof(s_topic_cmd),  AWS_TOPIC_CMD_FMT,  s_mac);

    prvWaitForProvisioning();
    /* NetworkCredentials are now filled inside aws_connect_tls() (shared handshake) each connect. */

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
        if (s_otaStarted) {
            ota_app_on_mqtt_disconnected();   /* OTA_Suspend -> reconnect OTA_Resume's (re-subscribes) */
        }
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
    /* Wait WITHOUT a timeout. subscribeArgs/subscribeInfo are stack locals the queued command holds
     * by POINTER, so they MUST outlive the command. A premature timeout let this return (reusing the
     * stack) while the SUBSCRIBE was still queued -> the agent later dereferenced a stale subscribeInfo
     * (garbage pTopicFilter) -> strncmp bus fault in validateSubscribeUnsubscribeParams on a busy
     * reconnect. Never hangs: the agent runs the command, or CancelAll on disconnect fires the callback. */
    ( void ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
    log_info("[AWS] subscribed '%s'", s_topic_cmd);
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
            int n = aws_build_telemetry_json(payload, sizeof(payload), s_mac, (unsigned)seq);

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
                /* Wait WITHOUT a timeout: pub + payload[] are stack locals the queued command holds by
                 * POINTER, so they must outlive the command. A premature timeout returned here (reusing
                 * the stack) while the publish was still queued -> the agent dereferenced a stale pub
                 * (same class of bug as the subscribe path). Always resolves: the agent serialises it,
                 * or CancelAll on disconnect fires the callback. */
                ( void ) ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
                log_info("[AWS] TX %s %s", s_topic_tele, payload);
                seq++;
            } else {
                log_error("[AWS] publish enqueue FAIL");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(AWS_TELEMETRY_PERIOD_MS));
    }
}

void aws_iot_demo_start(void)
{
    /* Start ONCE. The network-up hook (main.c) calls this on EVERY "network UP" - including a WiFi
     * reconnect - but the agent task already owns reconnection internally (its CommandLoop returns on
     * disconnect and the for(;;) re-runs prvAgentConnect once the link is back). Without this guard a
     * reconnect spawned a SECOND aws_agent task that raced the first on the shared s_agent / picotls
     * transport, corrupting the TLS send buffer -> HardFault in picotls buffer_push_encrypted_records
     * (confirmed on HW via OpenOCD: faulted task "aws_agent", CFSR=0x400 imprecise bus fault). */
    static int s_started = 0;
    if (s_started) {
        log_info("[AWS] already started; ignoring extra network-up (agent task handles reconnects)");
        return;
    }
    s_started = 1;

    /* 5120 words (20 KB) on AmebaD: MQTTAgent_CommandLoop + the one-shot TLS handshake (mbedTLS
     * RSA-2048 cert-chain verify peaks ~12 KB of stack). bl602 (AWS_AGENT_STACK_WORDS=2048) runs the
     * same in 8 KB (its handshake peak; proven by the telemetry client) and, under AWS_STATIC_TASKS,
     * takes the stack from .bss so a heap fragmented by WiFi+DHCP can't fail the (16 KB) contiguous
     * allocation - the observed `xTaskCreate(aws_agent) FAILED [free heap 45 KB]` on bl602. */
#ifdef AWS_STATIC_TASKS
    {
        static StackType_t  s_agentStack[ AWS_AGENT_STACK_WORDS ];
        static StaticTask_t s_agentTcb;
        if (xTaskCreateStatic(aws_agent_task, "aws_agent", AWS_AGENT_STACK_WORDS, NULL,
                              tskIDLE_PRIORITY + 3, s_agentStack, &s_agentTcb) == NULL)
            log_error("[AWS] xTaskCreateStatic(aws_agent) FAILED");
    }
#else
    if (xTaskCreate(aws_agent_task, "aws_agent", AWS_AGENT_STACK_WORDS, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        log_error("[AWS] xTaskCreate(aws_agent) FAILED");
#endif

#ifndef AWS_NO_TELEMETRY
    /* 1024 words (4 KB): `tasks` high-water shows the telemetry task only ever used ~1.7 KB. The bl602
     * OTA build omits it (AWS_NO_TELEMETRY) to save heap - OTA job status is the liveness signal there. */
#ifdef AWS_STATIC_TASKS
    {
        static StackType_t  s_teleStack[ AWS_TELE_STACK_WORDS ];
        static StaticTask_t s_teleTcb;
        if (xTaskCreateStatic(aws_telemetry_task, "aws_tele", AWS_TELE_STACK_WORDS, NULL,
                              tskIDLE_PRIORITY + 2, s_teleStack, &s_teleTcb) == NULL)
            log_error("[AWS] xTaskCreateStatic(aws_tele) FAILED");
    }
#else
    if (xTaskCreate(aws_telemetry_task, "aws_tele", AWS_TELE_STACK_WORDS, NULL,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS)
        log_error("[AWS] xTaskCreate(aws_tele) FAILED");
#endif
#endif /* !AWS_NO_TELEMETRY */
}
