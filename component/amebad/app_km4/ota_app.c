/*
 * ota_app.c - AWS IoT OTA Agent wiring for AmebaD KM4 (make AWS=ON).
 *
 * Connects the FreeRTOS OTA Agent (v3.3.0) to the shared coreMQTT-Agent connection:
 *  - OS interface  : the shipped ota_os_freertos.c (event queue + timers + malloc).
 *  - MQTT interface: subscribe/unsubscribe/publish wrappers that enqueue MQTTAgent_*
 *                    commands and BLOCK on a task-notification until the agent task's
 *                    command-complete callback fires. The subscribe wrapper also
 *                    registers an incoming-publish callback in the subscription_manager
 *                    keyed by the topic filter; that callback copies the payload into an
 *                    OtaEventData_t and OTA_SignalEvent()s the agent (job doc vs file
 *                    block, distinguished by the "/streams/" topic segment).
 *  - PAL interface : ota_pal.c (slot write via ota_flash.c + mbedTLS signature verify).
 *
 * The OTA agent runs in its own task (OTA_EventProcessingTask). On the first MQTT-Agent
 * connection, ota_app_init() OTA_Init()s and starts the task; ota_app_on_mqtt_connected()
 * signals OtaAgentEventStart (or a fresh OTA_CheckForUpdate after a reconnect) so the job
 * subscribe/check rides the live session. The subscription_manager re-registers the OTA
 * topic callbacks automatically because the OTA agent re-subscribes after a resume.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ota.h"
#include "ota_private.h"
#include "ota_os_freertos.h"
#include "ota_appversion32.h"

#include "core_mqtt_agent.h"
#include "subscription_manager.h"

#include "ota_app.h"
#include "at_log.h"

/* Use the app's flushed at_log (printf is fully-buffered per-task on the LOGUART, so [OTA] printf
 * lines never surface). The first vararg is always a string literal, so the "[OTA] " concat works. */
#define OTA_APP_LOG( ... )    log_info( "[OTA] " __VA_ARGS__ )

/* The PAL interface getter (ota_pal.c). */
extern const OtaPalInterface_t * ota_pal_get_interface( void );

/* The OTA library compares this against the job's version to reject downgrades / detect a
 * same-version self-test. ota.c declares it extern; the app must define it. Bump the build
 * field for each OTA-B image so the cloud-side version gate works. (The human-readable
 * AT_FW_VERSION string stamped into telemetry is independent of this numeric triple.) */
#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR    0
#endif
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR    9
#endif
#ifndef APP_VERSION_BUILD
#define APP_VERSION_BUILD    0
#endif
const AppVersion32_t appFirmwareVersion =
{
    .u.x.major = APP_VERSION_MAJOR,
    .u.x.minor = APP_VERSION_MINOR,
    .u.x.build = APP_VERSION_BUILD,
};

/* ------------------------------------------------------------- buffers -------- */
/* OtaAppBuffer_t static backing (sizes per the task brief). */
static uint8_t s_updateFilePath[ 256 ];
static uint8_t s_certFilePath[ 256 ];
static uint8_t s_streamName[ 64 ];
static uint8_t s_decodeMem[ OTA_FILE_BLOCK_SIZE ];     /* otaconfigLOG2_FILE_BLOCK_SIZE=12 -> 4096 */
static uint8_t s_fileBitmap[ OTA_MAX_BLOCK_BITMAP_SIZE ]; /* 128 */

/* OTA event-data buffer pool: the MQTT incoming-publish callback wraps a received
 * job-doc / file-block payload in one of these before OTA_SignalEvent(). The OTA agent
 * marks bufferUsed=false again once processed; we recycle from this pool. */
#ifndef OTA_APP_NUM_EVENT_BUFFERS
#define OTA_APP_NUM_EVENT_BUFFERS    ( 4U )
#endif
static OtaEventData_t s_eventBuffers[ OTA_APP_NUM_EVENT_BUFFERS ];
static SemaphoreHandle_t s_bufferMutex = NULL;
static StaticSemaphore_t s_bufferMutexStorage;

/* Shared MQTT-Agent context (owned by aws_iot_demo.c). */
static MQTTAgentContext_t * s_pAgent = NULL;

/* The OTA Agent interfaces (must stay in scope for the life of the agent). */
static OtaInterfaces_t s_otaInterfaces;
static OtaAppBuffer_t  s_otaBuffer;

static int s_initialised = 0;

/* ------------------------------------------------------- event-buffer pool ---- */
static OtaEventData_t * prvEventBufferGet( void )
{
    OtaEventData_t * pBuf = NULL;
    uint32_t i;

    if( s_bufferMutex == NULL )
    {
        return NULL;
    }

    if( xSemaphoreTake( s_bufferMutex, portMAX_DELAY ) == pdTRUE )
    {
        for( i = 0; i < OTA_APP_NUM_EVENT_BUFFERS; i++ )
        {
            if( s_eventBuffers[ i ].bufferUsed == false )
            {
                s_eventBuffers[ i ].bufferUsed = true;
                pBuf = &s_eventBuffers[ i ];
                break;
            }
        }
        ( void ) xSemaphoreGive( s_bufferMutex );
    }

    return pBuf;
}

/* ---------------------------------------------- MQTT-Agent command helpers ---- */
/* A blocking completion handshake: each wrapper enqueues an MQTTAgent_* command with a
 * completion callback that notifies the calling task and stashes the return code. */
typedef struct
{
    TaskHandle_t  task;
    MQTTStatus_t  returnCode;
} OtaMqttCmdCtx_t;

static void prvCmdCompleteCb( MQTTAgentCommandContext_t * pCmdCtx,
                              MQTTAgentReturnInfo_t * pReturnInfo )
{
    OtaMqttCmdCtx_t * pCtx = ( OtaMqttCmdCtx_t * ) pCmdCtx;

    if( pCtx != NULL )
    {
        pCtx->returnCode = pReturnInfo->returnCode;
        if( pCtx->task != NULL )
        {
            ( void ) xTaskNotifyGive( pCtx->task );
        }
    }
}

/* Block (up to timeoutMs) for the command-complete notification. */
#define OTA_AGENT_CMD_TIMEOUT_MS    ( 10000U )

static bool prvWaitForCmd( OtaMqttCmdCtx_t * pCtx )
{
    /* The completion callback runs in the agent task; ulTaskNotifyTake clears on exit. */
    return ( ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( OTA_AGENT_CMD_TIMEOUT_MS ) ) != 0 ) ? true : false;
}

/* --------------------------------------------- incoming-publish dispatcher ---- */
/* Registered with the subscription_manager for the OTA topic filters. Runs in the
 * agent task's incoming-publish callback. Routes the payload into the OTA event queue. */
static void prvOtaIncomingPublish( void * pvCtx, MQTTPublishInfo_t * pxPublish )
{
    OtaEventMsg_t eventMsg = { 0 };
    OtaEventData_t * pData;
    bool isStream;

    ( void ) pvCtx;

    if( ( pxPublish == NULL ) || ( pxPublish->payloadLength == 0U ) )
    {
        return;
    }

    if( pxPublish->payloadLength > sizeof( pData->data ) )
    {
        OTA_APP_LOG( "RX publish too large (%u > %u) - dropped",
                     ( unsigned ) pxPublish->payloadLength, ( unsigned ) sizeof( pData->data ) );
        return;
    }

    pData = prvEventBufferGet();
    if( pData == NULL )
    {
        OTA_APP_LOG( "RX publish - no free event buffer, dropped" );
        return;
    }

    memcpy( pData->data, pxPublish->pPayload, pxPublish->payloadLength );
    pData->dataLength = ( uint32_t ) pxPublish->payloadLength;

    /* AWS OTA data-plane (file blocks) arrive on "$aws/things/<thing>/streams/...";
     * everything else routed here is a job document. */
    isStream = ( pxPublish->topicNameLength >= 9U ) &&
               ( strstr( pxPublish->pTopicName, "/streams/" ) != NULL );

    eventMsg.pEventData = pData;
    eventMsg.eventId = isStream ? OtaAgentEventReceivedFileBlock
                                : OtaAgentEventReceivedJobDocument;

    if( OTA_SignalEvent( &eventMsg ) == false )
    {
        /* Could not enqueue: release the buffer so it is not leaked. */
        pData->bufferUsed = false;
        OTA_APP_LOG( "RX publish - OTA_SignalEvent FAIL, buffer released" );
    }
}

/* ------------------------------------------------ OTA MQTT interface impl ----- */
static OtaMqttStatus_t prvOtaMqttSubscribe( const char * pTopicFilter,
                                            uint16_t topicFilterLength,
                                            uint8_t ucQoS )
{
    MQTTStatus_t st;
    MQTTAgentSubscribeArgs_t subscribeArgs;
    MQTTSubscribeInfo_t subscribeInfo;
    MQTTAgentCommandInfo_t commandInfo = { 0 };
    OtaMqttCmdCtx_t ctx;

    if( s_pAgent == NULL )
    {
        return OtaMqttSubscribeFailed;
    }

    /* Register the routing callback BEFORE the SUBACK so no early publish is missed. */
    if( SubscriptionManager_AddSubscription( pTopicFilter, topicFilterLength,
                                             prvOtaIncomingPublish, NULL ) == false )
    {
        OTA_APP_LOG( "subscribe: subscription table full for '%.*s'",
                     ( int ) topicFilterLength, pTopicFilter );
        return OtaMqttSubscribeFailed;
    }

    memset( &subscribeInfo, 0, sizeof( subscribeInfo ) );
    subscribeInfo.qos = ( MQTTQoS_t ) ucQoS;
    subscribeInfo.pTopicFilter = pTopicFilter;
    subscribeInfo.topicFilterLength = topicFilterLength;
    subscribeArgs.pSubscribeInfo = &subscribeInfo;
    subscribeArgs.numSubscriptions = 1U;
    subscribeArgs.pProperties = NULL;

    ctx.task = xTaskGetCurrentTaskHandle();
    ctx.returnCode = MQTTSendFailed;
    commandInfo.cmdCompleteCallback = prvCmdCompleteCb;
    commandInfo.pCmdCompleteCallbackContext = ( MQTTAgentCommandContext_t * ) &ctx;
    commandInfo.blockTimeMs = OTA_AGENT_CMD_TIMEOUT_MS;

    st = MQTTAgent_Subscribe( s_pAgent, &subscribeArgs, &commandInfo );
    if( ( st != MQTTSuccess ) || ( prvWaitForCmd( &ctx ) == false ) || ( ctx.returnCode != MQTTSuccess ) )
    {
        OTA_APP_LOG( "subscribe '%.*s' FAIL (enqueue=%d ret=%d)",
                     ( int ) topicFilterLength, pTopicFilter, ( int ) st, ( int ) ctx.returnCode );
        SubscriptionManager_RemoveSubscription( pTopicFilter, topicFilterLength );
        return OtaMqttSubscribeFailed;
    }

    OTA_APP_LOG( "subscribed '%.*s' (qos %u)", ( int ) topicFilterLength, pTopicFilter, ucQoS );
    return OtaMqttSuccess;
}

static OtaMqttStatus_t prvOtaMqttUnsubscribe( const char * pTopicFilter,
                                              uint16_t topicFilterLength,
                                              uint8_t ucQoS )
{
    MQTTStatus_t st;
    MQTTAgentSubscribeArgs_t subscribeArgs;
    MQTTSubscribeInfo_t subscribeInfo;
    MQTTAgentCommandInfo_t commandInfo = { 0 };
    OtaMqttCmdCtx_t ctx;

    if( s_pAgent == NULL )
    {
        return OtaMqttUnsubscribeFailed;
    }

    memset( &subscribeInfo, 0, sizeof( subscribeInfo ) );
    subscribeInfo.qos = ( MQTTQoS_t ) ucQoS;
    subscribeInfo.pTopicFilter = pTopicFilter;
    subscribeInfo.topicFilterLength = topicFilterLength;
    subscribeArgs.pSubscribeInfo = &subscribeInfo;
    subscribeArgs.numSubscriptions = 1U;
    subscribeArgs.pProperties = NULL;

    ctx.task = xTaskGetCurrentTaskHandle();
    ctx.returnCode = MQTTSendFailed;
    commandInfo.cmdCompleteCallback = prvCmdCompleteCb;
    commandInfo.pCmdCompleteCallbackContext = ( MQTTAgentCommandContext_t * ) &ctx;
    commandInfo.blockTimeMs = OTA_AGENT_CMD_TIMEOUT_MS;

    st = MQTTAgent_Unsubscribe( s_pAgent, &subscribeArgs, &commandInfo );
    if( ( st != MQTTSuccess ) || ( prvWaitForCmd( &ctx ) == false ) || ( ctx.returnCode != MQTTSuccess ) )
    {
        OTA_APP_LOG( "unsubscribe '%.*s' FAIL (enqueue=%d ret=%d)",
                     ( int ) topicFilterLength, pTopicFilter, ( int ) st, ( int ) ctx.returnCode );
        /* Remove the routing entry regardless so we stop dispatching. */
        SubscriptionManager_RemoveSubscription( pTopicFilter, topicFilterLength );
        return OtaMqttUnsubscribeFailed;
    }

    SubscriptionManager_RemoveSubscription( pTopicFilter, topicFilterLength );
    OTA_APP_LOG( "unsubscribed '%.*s'", ( int ) topicFilterLength, pTopicFilter );
    return OtaMqttSuccess;
}

static OtaMqttStatus_t prvOtaMqttPublish( const char * const pacTopic,
                                          uint16_t usTopicLen,
                                          const char * pcMsg,
                                          uint32_t ulMsgSize,
                                          uint8_t ucQoS )
{
    MQTTStatus_t st;
    MQTTPublishInfo_t publishInfo;
    MQTTAgentPublishArgs_t publishArgs;
    MQTTAgentCommandInfo_t commandInfo = { 0 };
    OtaMqttCmdCtx_t ctx;

    if( s_pAgent == NULL )
    {
        return OtaMqttPublishFailed;
    }

    memset( &publishInfo, 0, sizeof( publishInfo ) );
    publishInfo.qos = ( MQTTQoS_t ) ucQoS;
    publishInfo.pTopicName = pacTopic;
    publishInfo.topicNameLength = usTopicLen;
    publishInfo.pPayload = pcMsg;
    publishInfo.payloadLength = ulMsgSize;

    publishArgs.pPublishInfo = &publishInfo;
    publishArgs.pProperties = NULL;

    ctx.task = xTaskGetCurrentTaskHandle();
    ctx.returnCode = MQTTSendFailed;
    commandInfo.cmdCompleteCallback = prvCmdCompleteCb;
    commandInfo.pCmdCompleteCallbackContext = ( MQTTAgentCommandContext_t * ) &ctx;
    commandInfo.blockTimeMs = OTA_AGENT_CMD_TIMEOUT_MS;

    st = MQTTAgent_Publish( s_pAgent, &publishArgs, &commandInfo );
    if( ( st != MQTTSuccess ) || ( prvWaitForCmd( &ctx ) == false ) || ( ctx.returnCode != MQTTSuccess ) )
    {
        OTA_APP_LOG( "publish '%.*s' FAIL (enqueue=%d ret=%d)",
                     ( int ) usTopicLen, pacTopic, ( int ) st, ( int ) ctx.returnCode );
        return OtaMqttPublishFailed;
    }

    return OtaMqttSuccess;
}

/* ----------------------------------------------------- OTA app callback ------- */
static void prvOtaAppCallback( OtaJobEvent_t eEvent, const void * pData )
{
    OtaErr_t err;

    switch( eEvent )
    {
        case OtaJobEventActivate:
            OTA_APP_LOG( "job: image ready -> activate (reboot)" );
            err = OTA_ActivateNewImage();   /* should not return (reboots) */
            OTA_APP_LOG( "OTA_ActivateNewImage returned %d (%s)", ( int ) err, OTA_Err_strerror( err ) );
            break;

        case OtaJobEventFail:
            OTA_APP_LOG( "job: FAILED" );
            break;

        case OtaJobEventStartTest:
            /* SIMPLIFICATION: no real self-test - accept immediately. */
            OTA_APP_LOG( "job: self-test -> accepting image (no self-test implemented)" );
            err = OTA_SetImageState( OtaImageStateAccepted );
            if( err != OtaErrNone )
            {
                OTA_APP_LOG( "OTA_SetImageState(Accepted) FAIL %d (%s)", ( int ) err, OTA_Err_strerror( err ) );
            }
            break;

        case OtaJobEventProcessed:
            /* The OtaEventData buffer for a processed event can be recycled. */
            if( pData != NULL )
            {
                ( ( OtaEventData_t * ) pData )->bufferUsed = false;
            }
            break;

        case OtaJobEventUpdateComplete:
            OTA_APP_LOG( "job: update complete" );
            break;

        default:
            OTA_APP_LOG( "job: event %d", ( int ) eEvent );
            break;
    }
}

/* ------------------------------------------------------------ public API ------ */
int ota_app_init( MQTTAgentContext_t * pAgent, const char * pThingName )
{
    OtaErr_t err;

    if( s_initialised )
    {
        /* Already up (e.g. a reconnect re-invoked the connect hook): just refresh the
         * agent pointer and let ota_app_on_mqtt_connected() handle the resume. */
        s_pAgent = pAgent;
        return 0;
    }

    if( ( pAgent == NULL ) || ( pThingName == NULL ) )
    {
        return -1;
    }

    s_pAgent = pAgent;

    s_bufferMutex = xSemaphoreCreateMutexStatic( &s_bufferMutexStorage );
    if( s_bufferMutex == NULL )
    {
        return -1;
    }
    memset( s_eventBuffers, 0, sizeof( s_eventBuffers ) );

    /* OTA application buffers. */
    memset( &s_otaBuffer, 0, sizeof( s_otaBuffer ) );
    s_otaBuffer.pUpdateFilePath    = s_updateFilePath;
    s_otaBuffer.updateFilePathsize = sizeof( s_updateFilePath );
    s_otaBuffer.pCertFilePath      = s_certFilePath;
    s_otaBuffer.certFilePathSize   = sizeof( s_certFilePath );
    s_otaBuffer.pStreamName        = s_streamName;
    s_otaBuffer.streamNameSize     = sizeof( s_streamName );
    s_otaBuffer.pDecodeMemory      = s_decodeMem;
    s_otaBuffer.decodeMemorySize   = sizeof( s_decodeMem );
    s_otaBuffer.pFileBitmap        = s_fileBitmap;
    s_otaBuffer.fileBitmapSize     = sizeof( s_fileBitmap );
    /* URL / auth-scheme buffers stay NULL: MQTT data plane only (no HTTP). */

    /* OTA interfaces. */
    memset( &s_otaInterfaces, 0, sizeof( s_otaInterfaces ) );

    s_otaInterfaces.os.event.init   = OtaInitEvent_FreeRTOS;
    s_otaInterfaces.os.event.send   = OtaSendEvent_FreeRTOS;
    s_otaInterfaces.os.event.recv   = OtaReceiveEvent_FreeRTOS;
    s_otaInterfaces.os.event.deinit = OtaDeinitEvent_FreeRTOS;
    s_otaInterfaces.os.timer.start  = OtaStartTimer_FreeRTOS;
    s_otaInterfaces.os.timer.stop   = OtaStopTimer_FreeRTOS;
    s_otaInterfaces.os.timer.delete = OtaDeleteTimer_FreeRTOS;
    s_otaInterfaces.os.mem.malloc   = Malloc_FreeRTOS;
    s_otaInterfaces.os.mem.free     = Free_FreeRTOS;

    s_otaInterfaces.mqtt.subscribe   = prvOtaMqttSubscribe;
    s_otaInterfaces.mqtt.unsubscribe = prvOtaMqttUnsubscribe;
    s_otaInterfaces.mqtt.publish     = prvOtaMqttPublish;

    /* http interface left zeroed (unused: MQTT data plane). */

    s_otaInterfaces.pal = *ota_pal_get_interface();

    err = OTA_Init( &s_otaBuffer, &s_otaInterfaces,
                    ( const uint8_t * ) pThingName, prvOtaAppCallback );
    if( err != OtaErrNone )
    {
        OTA_APP_LOG( "OTA_Init FAIL %d (%s)", ( int ) err, OTA_Err_strerror( err ) );
        return -1;
    }

    /* Dedicated OTA agent task: OTA_EventProcessingTask runs the state machine. */
    if( xTaskCreate( OTA_EventProcessingTask, "ota_agent",
                     otaconfigSTACK_SIZE, NULL,
                     otaconfigAGENT_PRIORITY, NULL ) != pdPASS )
    {
        OTA_APP_LOG( "xTaskCreate(ota_agent) FAIL" );
        return -1;
    }

    s_initialised = 1;
    OTA_APP_LOG( "agent initialised (thing '%s')", pThingName );
    return 0;
}

void ota_app_on_mqtt_connected( void )
{
    OtaEventMsg_t eventMsg = { 0 };
    OtaState_t state;

    if( !s_initialised )
    {
        return;
    }

    state = OTA_GetState();

    if( state == OtaAgentStateSuspended )
    {
        OTA_APP_LOG( "MQTT reconnected -> OTA_Resume" );
        ( void ) OTA_Resume();
    }
    else if( state == OtaAgentStateStopped )
    {
        OTA_APP_LOG( "MQTT connected -> OtaAgentEventStart" );
        eventMsg.eventId = OtaAgentEventStart;
        ( void ) OTA_SignalEvent( &eventMsg );
    }
    else
    {
        /* Init / Ready: kick the state machine to request a job. */
        OTA_APP_LOG( "MQTT connected -> OtaAgentEventStart (state %d)", ( int ) state );
        eventMsg.eventId = OtaAgentEventStart;
        ( void ) OTA_SignalEvent( &eventMsg );
    }
}
