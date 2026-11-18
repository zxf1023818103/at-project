/*
 * freertos_command_pool.h - static pool of MQTTAgentCommand_t structures for the
 * coreMQTT-Agent, with a counting semaphore guarding availability.
 *
 * Standard AWS reference port (Labs-Project-FreeRTOS / coreMQTT-Agent demos),
 * adapted for the AmebaD KM4 AWS image. The agent's MQTTAgentMessageInterface_t
 * uses Agent_GetCommand / Agent_ReleaseCommand to allocate/free the command
 * structures it queues; backing them with a fixed static pool avoids heap churn.
 */
#ifndef FREERTOS_COMMAND_POOL_H
#define FREERTOS_COMMAND_POOL_H

#include "core_mqtt_agent.h"

/**
 * @brief Obtain a command from the pool, blocking up to blockTimeMs.
 *
 * @return A pointer to a zeroed MQTTAgentCommand_t, or NULL on timeout.
 */
MQTTAgentCommand_t * Agent_GetCommand( uint32_t blockTimeMs );

/**
 * @brief Return a command to the pool.
 *
 * @return true if the command belonged to the pool and was released.
 */
bool Agent_ReleaseCommand( MQTTAgentCommand_t * pCommandToRelease );

#endif /* FREERTOS_COMMAND_POOL_H */
