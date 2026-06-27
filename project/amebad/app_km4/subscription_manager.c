/*
 * subscription_manager.c - topic-filter dispatch table for the shared MQTT-Agent
 * connection. Standard AWS reference port, using coreMQTT's MQTT_MatchTopic() for
 * wildcard ('+'/'#') matching.
 *
 * NOTE on threading: AddSubscription/RemoveSubscription are called from the agent
 * task's command-complete callbacks (which run in the agent task), and
 * HandleIncomingPublishes runs in the agent task's incoming-publish callback. All
 * table access is therefore from the single agent task, so no extra locking is
 * needed (matching the AWS reference). taskENTER_CRITICAL guards remain for safety
 * in case a future caller mutates from another task.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "subscription_manager.h"
#include "core_mqtt.h"

static SubscriptionElement_t xSubscriptions[ SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS ];

bool SubscriptionManager_AddSubscription( const char * pcTopicFilterString,
                                          uint16_t usTopicFilterLength,
                                          IncomingPubCallback_t pxIncomingPublishCallback,
                                          void * pvIncomingPublishCallbackContext )
{
    int32_t i;
    int32_t freeIndex = -1;
    bool ret = false;

    if( ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) ||
        ( usTopicFilterLength >= SUBSCRIPTION_MANAGER_TOPIC_FILTER_MAX ) ||
        ( pxIncomingPublishCallback == NULL ) )
    {
        return false;
    }

    taskENTER_CRITICAL();
    {
        for( i = 0; i < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; i++ )
        {
            if( xSubscriptions[ i ].usFilterStringLength == 0U )
            {
                if( freeIndex == -1 )
                {
                    freeIndex = i;
                }
            }
            else if( ( xSubscriptions[ i ].usFilterStringLength == usTopicFilterLength ) &&
                     ( strncmp( xSubscriptions[ i ].pcSubscriptionFilterString,
                                pcTopicFilterString, usTopicFilterLength ) == 0 ) )
            {
                /* Already registered: overwrite the callback/context (idempotent re-subscribe). */
                xSubscriptions[ i ].pxIncomingPublishCallback = pxIncomingPublishCallback;
                xSubscriptions[ i ].pvIncomingPublishCallbackContext = pvIncomingPublishCallbackContext;
                freeIndex = i;
                ret = true;
                break;
            }
        }

        if( ( ret == false ) && ( freeIndex != -1 ) )
        {
            xSubscriptions[ freeIndex ].usFilterStringLength = usTopicFilterLength;
            memcpy( xSubscriptions[ freeIndex ].pcSubscriptionFilterString,
                    pcTopicFilterString, usTopicFilterLength );
            xSubscriptions[ freeIndex ].pcSubscriptionFilterString[ usTopicFilterLength ] = '\0';
            xSubscriptions[ freeIndex ].pxIncomingPublishCallback = pxIncomingPublishCallback;
            xSubscriptions[ freeIndex ].pvIncomingPublishCallbackContext = pvIncomingPublishCallbackContext;
            ret = true;
        }
    }
    taskEXIT_CRITICAL();

    return ret;
}

void SubscriptionManager_RemoveSubscription( const char * pcTopicFilterString,
                                             uint16_t usTopicFilterLength )
{
    int32_t i;

    if( ( pcTopicFilterString == NULL ) || ( usTopicFilterLength == 0U ) )
    {
        return;
    }

    taskENTER_CRITICAL();
    {
        for( i = 0; i < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; i++ )
        {
            if( ( xSubscriptions[ i ].usFilterStringLength == usTopicFilterLength ) &&
                ( strncmp( xSubscriptions[ i ].pcSubscriptionFilterString,
                           pcTopicFilterString, usTopicFilterLength ) == 0 ) )
            {
                memset( &xSubscriptions[ i ], 0x00, sizeof( SubscriptionElement_t ) );
            }
        }
    }
    taskEXIT_CRITICAL();
}

bool SubscriptionManager_HandleIncomingPublishes( MQTTPublishInfo_t * pxPublishInfo )
{
    int32_t i;
    bool isMatched = false;
    bool publishHandled = false;

    if( pxPublishInfo == NULL )
    {
        return false;
    }

    for( i = 0; i < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; i++ )
    {
        if( xSubscriptions[ i ].usFilterStringLength == 0U )
        {
            continue;
        }

        isMatched = false;
        ( void ) MQTT_MatchTopic( pxPublishInfo->pTopicName,
                                  pxPublishInfo->topicNameLength,
                                  xSubscriptions[ i ].pcSubscriptionFilterString,
                                  xSubscriptions[ i ].usFilterStringLength,
                                  &isMatched );

        if( isMatched && ( xSubscriptions[ i ].pxIncomingPublishCallback != NULL ) )
        {
            xSubscriptions[ i ].pxIncomingPublishCallback(
                xSubscriptions[ i ].pvIncomingPublishCallbackContext,
                pxPublishInfo );
            publishHandled = true;
        }
    }

    return publishHandled;
}
