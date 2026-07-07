/*
 * aws_connect.c - the shared TLS + MQTT connect handshake for both AWS IoT clients.
 *
 * The credential-load + NetworkCredentials fill + TLS_FreeRTOS_Connect + TransportInterface + MQTTConnectInfo
 * are identical between AmebaD's coreMQTT-Agent client and bl602's direct-coreMQTT telemetry client; only the
 * MQTT(Agent)_Init + MQTT_Connect differ (kept in each caller). This centralizes the shared part.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "aws_connect.h"

int aws_connect_tls( NetworkContext_t *      net,
                     NetworkCredentials_t *  creds,
                     TransportInterface_t *  transport,
                     MQTTConnectInfo_t *     connectInfo,
                     const aws_connect_params_t * p )
{
    TlsTransportStatus_t ts;

    /* Mutual-TLS creds: server trust = the CA at caPath (a file, or a folder of roots on AmebaD), client
     * cert from a LittleFS file, client key in RAM. SNI on (AWS requires it). rxReserveSize / backendMode
     * carry the picotls-TLS1.3-vs-default tuning (AmebaD forces picotls + a 6 KB rx reserve; bl602 = 0). */
    memset( creds, 0, sizeof( *creds ) );
    creds->pRootCaPath     = p->caPath;
    creds->pClientCertPath = p->certPath;
    creds->pPrivateKey     = p->key;
    creds->privateKeySize  = p->keyLen;
    creds->disableSni      = 0;
    creds->rxReserveSize   = p->rxReserveSize;
    creds->backendMode     = ( TlsBackendMode_t ) p->backendMode;

    printf( "[AWS] TLS connect %s:%u ...\r\n", p->endpoint, ( unsigned ) p->port );
    ts = TLS_FreeRTOS_Connect( net, p->endpoint, p->port, creds, p->recvTmoMs, p->sendTmoMs );
    if( ts != TLS_TRANSPORT_SUCCESS )
    {
        printf( "[AWS] TLS_FreeRTOS_Connect FAIL (%d)\r\n", ( int ) ts );
        TLS_FreeRTOS_Disconnect( net );   /* free whatever Connect allocated before failing (no leak on retry) */
        return -1;
    }

    transport->pNetworkContext = net;
    transport->send   = TLS_FreeRTOS_send;
    transport->recv   = TLS_FreeRTOS_recv;
    transport->writev = NULL;

    memset( connectInfo, 0, sizeof( *connectInfo ) );
    connectInfo->cleanSession           = true;
    connectInfo->pClientIdentifier      = p->clientId;
    connectInfo->clientIdentifierLength = ( uint16_t ) strlen( p->clientId );
    connectInfo->keepAliveSeconds       = p->keepAliveS;

    return 0;
}
