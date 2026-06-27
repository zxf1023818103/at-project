/*
 * freertos_agent_message.c - FreeRTOS-queue MQTTAgentMessageInterface_t backend.
 *
 * Standard AWS reference port. The queue stores MQTTAgentCommand_t* (pointer-sized
 * items); xQueueSend/xQueueReceive are already thread-safe so no extra locking.
 */
#include "freertos_agent_message.h"
#include "core_mqtt_agent.h"

bool Agent_MessageSend( MQTTAgentMessageContext_t * pMsgCtx,
                        MQTTAgentCommand_t * const * pCommandToSend,
                        uint32_t blockTimeMs )
{
    BaseType_t ret = pdFALSE;

    if( ( pMsgCtx != NULL ) && ( pMsgCtx->queue != NULL ) && ( pCommandToSend != NULL ) )
    {
        ret = xQueueSendToBack( pMsgCtx->queue, pCommandToSend, pdMS_TO_TICKS( blockTimeMs ) );
    }

    return ( ret == pdTRUE ) ? true : false;
}

bool Agent_MessageReceive( MQTTAgentMessageContext_t * pMsgCtx,
                           MQTTAgentCommand_t ** pReceivedCommand,
                           uint32_t blockTimeMs )
{
    BaseType_t ret = pdFALSE;

    if( ( pMsgCtx != NULL ) && ( pMsgCtx->queue != NULL ) && ( pReceivedCommand != NULL ) )
    {
        ret = xQueueReceive( pMsgCtx->queue, pReceivedCommand, pdMS_TO_TICKS( blockTimeMs ) );
    }

    return ( ret == pdTRUE ) ? true : false;
}
