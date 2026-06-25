/*
 * freertos_agent_message.h - MQTTAgentMessageInterface_t backend over a FreeRTOS
 * queue of MQTTAgentCommand_t* for the coreMQTT-Agent.
 *
 * Standard AWS reference port. The agent task and the application tasks rendezvous
 * on a single thread-safe queue: application tasks Agent_MessageSend() a command
 * pointer, the agent task Agent_MessageReceive()s it in its command loop.
 */
#ifndef FREERTOS_AGENT_MESSAGE_H
#define FREERTOS_AGENT_MESSAGE_H

#include "FreeRTOS.h"
#include "queue.h"

#include "core_mqtt_agent.h"
#include "core_mqtt_agent_message_interface.h"

/**
 * @brief The concrete message context: a FreeRTOS queue holding command pointers.
 *
 * coreMQTT-Agent forward-declares `struct MQTTAgentMessageContext`; the consumer
 * gives it a definition. The agent only ever passes a pointer to it through the
 * send/recv callbacks below, so the definition is private to this port.
 */
struct MQTTAgentMessageContext
{
    QueueHandle_t queue;
};

/**
 * @brief Post a command pointer to the agent's queue. Thread-safe.
 */
bool Agent_MessageSend( MQTTAgentMessageContext_t * pMsgCtx,
                        MQTTAgentCommand_t * const * pCommandToSend,
                        uint32_t blockTimeMs );

/**
 * @brief Receive a command pointer from the agent's queue. Thread-safe.
 */
bool Agent_MessageReceive( MQTTAgentMessageContext_t * pMsgCtx,
                           MQTTAgentCommand_t ** pReceivedCommand,
                           uint32_t blockTimeMs );

#endif /* FREERTOS_AGENT_MESSAGE_H */
