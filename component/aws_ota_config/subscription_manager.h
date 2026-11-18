/*
 * subscription_manager.h - topic-filter -> incoming-publish-callback table for the
 * coreMQTT-Agent shared connection.
 *
 * Standard AWS reference port (aws-iot-device-sdk-embedded-C / FreeRTOS demos).
 * The agent's single MQTTAgentIncomingPublishCallback_t routes every incoming
 * PUBLISH here; handleIncomingPublishes() matches the topic against the registered
 * filters (with MQTT '+'/'#' wildcards) and fans out to each subscriber's callback.
 */
#ifndef SUBSCRIPTION_MANAGER_H
#define SUBSCRIPTION_MANAGER_H

#include "core_mqtt.h"
#include "core_mqtt_agent.h"

/* Maximum number of distinct topic-filter subscriptions tracked. The OTA agent
 * registers the job + stream topics (a handful) and the telemetry path adds the
 * command topic, so a small table is enough. */
#define SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS    ( 10 )

/* Maximum stored length of a topic filter (incl. NUL not required - length tracked). */
#define SUBSCRIPTION_MANAGER_TOPIC_FILTER_MAX     ( 100 )

/**
 * @brief Incoming-publish callback signature for a registered topic filter.
 *
 * @param[in] pvIncomingPublishCallbackContext The context registered alongside the filter.
 * @param[in] pxPublishInfo The deserialized publish.
 */
typedef void (* IncomingPubCallback_t )( void * pvIncomingPublishCallbackContext,
                                         MQTTPublishInfo_t * pxPublishInfo );

typedef struct SubscriptionElement
{
    IncomingPubCallback_t pxIncomingPublishCallback;
    void * pvIncomingPublishCallbackContext;
    uint16_t usFilterStringLength;
    char pcSubscriptionFilterString[ SUBSCRIPTION_MANAGER_TOPIC_FILTER_MAX ];
} SubscriptionElement_t;

/**
 * @brief Add a topic-filter subscription to the table.
 *
 * @return true if added (or already present), false if the table is full.
 */
bool SubscriptionManager_AddSubscription( const char * pcTopicFilterString,
                                          uint16_t usTopicFilterLength,
                                          IncomingPubCallback_t pxIncomingPublishCallback,
                                          void * pvIncomingPublishCallbackContext );

/**
 * @brief Remove a topic-filter subscription from the table.
 */
void SubscriptionManager_RemoveSubscription( const char * pcTopicFilterString,
                                             uint16_t usTopicFilterLength );

/**
 * @brief Route an incoming publish to every matching registered subscriber.
 *
 * Intended to be called from the agent's incoming-publish callback.
 *
 * @return true if at least one subscriber matched the topic.
 */
bool SubscriptionManager_HandleIncomingPublishes( MQTTPublishInfo_t * pxPublishInfo );

#endif /* SUBSCRIPTION_MANAGER_H */
