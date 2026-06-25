/*
 * freertos_command_pool.c - static command pool for the coreMQTT-Agent.
 *
 * Standard AWS reference port. A fixed array of MQTTAgentCommand_t guarded by a
 * counting semaphore: Agent_GetCommand() takes one (blocking), Agent_ReleaseCommand()
 * gives it back. No dynamic allocation.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "freertos_command_pool.h"
#include "core_mqtt_agent.h"

/* Number of command structures in the pool. Must be >= the maximum number of
 * commands that can be in flight (queued + awaiting ack) concurrently. The
 * telemetry task and the OTA task each enqueue one blocking command at a time,
 * plus the agent's own ProcessLoop wakeups, so a modest pool suffices. */
#define AGENT_COMMAND_POOL_SIZE    ( 10U )

static MQTTAgentCommand_t commandStructurePool[ AGENT_COMMAND_POOL_SIZE ];
static SemaphoreHandle_t   pFreeCommandStructMutex = NULL;
static StaticSemaphore_t   freeCommandStructMutexStorage;

static void initializePool( void )
{
    if( pFreeCommandStructMutex == NULL )
    {
        memset( commandStructurePool, 0, sizeof( commandStructurePool ) );
        pFreeCommandStructMutex = xSemaphoreCreateCountingStatic( AGENT_COMMAND_POOL_SIZE,
                                                                  AGENT_COMMAND_POOL_SIZE,
                                                                  &freeCommandStructMutexStorage );
        configASSERT( pFreeCommandStructMutex != NULL );
    }
}

MQTTAgentCommand_t * Agent_GetCommand( uint32_t blockTimeMs )
{
    MQTTAgentCommand_t * pStructToReturn = NULL;
    size_t i;

    initializePool();

    /* Wait for a command structure to become available. */
    if( xSemaphoreTake( pFreeCommandStructMutex, pdMS_TO_TICKS( blockTimeMs ) ) == pdTRUE )
    {
        /* Find a free entry; taskENTER_CRITICAL keeps the scan + claim atomic across tasks. */
        taskENTER_CRITICAL();
        {
            for( i = 0; i < AGENT_COMMAND_POOL_SIZE; i++ )
            {
                if( commandStructurePool[ i ].commandType == NONE )
                {
                    /* Mark in use with a non-NONE sentinel; the agent overwrites commandType. */
                    pStructToReturn = &commandStructurePool[ i ];
                    memset( pStructToReturn, 0x00, sizeof( MQTTAgentCommand_t ) );
                    pStructToReturn->commandType = NUM_COMMANDS; /* in-use marker */
                    break;
                }
            }
        }
        taskEXIT_CRITICAL();
    }

    return pStructToReturn;
}

bool Agent_ReleaseCommand( MQTTAgentCommand_t * pCommandToRelease )
{
    bool ret = false;
    size_t i;

    initializePool();

    for( i = 0; i < AGENT_COMMAND_POOL_SIZE; i++ )
    {
        if( pCommandToRelease == &commandStructurePool[ i ] )
        {
            taskENTER_CRITICAL();
            {
                pCommandToRelease->commandType = NONE;
            }
            taskEXIT_CRITICAL();
            ( void ) xSemaphoreGive( pFreeCommandStructMutex );
            ret = true;
            break;
        }
    }

    return ret;
}
