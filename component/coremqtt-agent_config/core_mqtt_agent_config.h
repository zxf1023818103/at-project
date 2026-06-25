/*
 * core_mqtt_agent_config.h — at-project configuration for coreMQTT-Agent (component
 * coremqtt-agent_config).
 *
 * Active because coremqtt-agent_config does NOT define MQTT_AGENT_DO_NOT_USE_CUSTOM_CONFIG;
 * anything not overridden here falls back to core_mqtt_agent_config_defaults.h
 * (MQTT_AGENT_MAX_OUTSTANDING_ACKS, MQTT_AGENT_MAX_EVENT_QUEUE_WAIT_TIME, the assert/log
 * macros, ...). Note core_mqtt_agent_config_defaults.h also chains into
 * core_mqtt_config_defaults.h, so the coreMQTT defaults apply too. Tune those here as
 * consumers need them.
 *
 * Logging: LogError / LogWarn / LogInfo / LogDebug default to no-ops in
 * core_mqtt_agent_config_defaults.h. Map them to the platform console here when wanted, e.g.
 *     #include <stdio.h>
 *     #define LogError( message )  do { printf( "[MQTTAgent] " ); printf message; printf( "\r\n" ); } while( 0 )
 * (coreMQTT-Agent passes the format+args already parenthesised, so `printf message` works.)
 *
 * NB: the agent needs an MQTTAgentMessageInterface_t (the command-queue backend, typically
 * over a FreeRTOS queue) supplied by the consumer at MQTTAgent_Init() — it is not a compile
 * macro and is not provided here.
 */
#ifndef CORE_MQTT_AGENT_CONFIG_H
#define CORE_MQTT_AGENT_CONFIG_H

/* Error/Warn routed to the console as [MQTTAgent] lines (Info/Debug stay no-ops to keep
 * the shared connection's hot path quiet). coreMQTT-Agent passes the format+args already
 * parenthesised, so `printf message` works. */
#include <stdio.h>
#define LogError( message )    do { printf( "[MQTTAgent] ERR " ); printf message; printf( "\r\n" ); } while( 0 )
#define LogWarn( message )     do { printf( "[MQTTAgent] WRN " ); printf message; printf( "\r\n" ); } while( 0 )
#define LogInfo( message )
#define LogDebug( message )

#endif /* CORE_MQTT_AGENT_CONFIG_H */
