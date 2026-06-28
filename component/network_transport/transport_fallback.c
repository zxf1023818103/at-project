/*
 * transport_fallback.c — TLS-1.2 fallback dispatcher.
 *
 * Tries the picotls backend (TLS 1.3 only) first; if its handshake fails (the usual reason being a
 * server that doesn't offer TLS 1.3), transparently retries with the mbedTLS backend (TLS 1.2/1.3).
 * Both backends are compiled with their public API renamed (picotls_TLS_*, mbedtls_TLS_*; see the
 * network_transport CMake), so they link side by side and this file calls them directly. The
 * winning backend + its params block are stashed in the app's NetworkContext, and send / recv /
 * Disconnect dispatch to it.
 */
#include <string.h>

#include "logging_levels.h"
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "TlsFallback"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif
#include "logging_stack.h"

#include "FreeRTOS.h"   /* pvPortMalloc / vPortFree */

#include "transport_fallback.h"

/* Each consuming TU defines struct NetworkContext (the transport leaves it incomplete). Layout is
 * just { TlsTransportParams_t * pParams } — identical in every backend, so the temporary contexts
 * we build below are ABI-compatible with the renamed backend entry points. */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

#define FB_BACKEND_NONE       0
#define FB_BACKEND_PICOTLS    1
#define FB_BACKEND_MBEDTLS    2

/* Renamed backend entry points (defined in network_transport_picotls / network_transport_mbedtls,
 * built with TLS_TRANSPORT_RENAME_PICOTLS / _MBEDTLS). NetworkContext_t / NetworkCredentials_t /
 * TlsTransportStatus_t have one shared layout (transport_tls_common.h), so these prototypes match. */
extern TlsTransportStatus_t picotls_TLS_FreeRTOS_Connect( NetworkContext_t *, const char *, uint16_t,
                                                          const NetworkCredentials_t *, uint32_t, uint32_t );
extern void    picotls_TLS_FreeRTOS_Disconnect( NetworkContext_t * );
extern int32_t picotls_TLS_FreeRTOS_recv( NetworkContext_t *, void *, size_t );
extern int32_t picotls_TLS_FreeRTOS_send( NetworkContext_t *, const void *, size_t );
extern size_t  picotls_TLS_FreeRTOS_params_size( void );

extern TlsTransportStatus_t mbedtls_TLS_FreeRTOS_Connect( NetworkContext_t *, const char *, uint16_t,
                                                          const NetworkCredentials_t *, uint32_t, uint32_t );
extern void    mbedtls_TLS_FreeRTOS_Disconnect( NetworkContext_t * );
extern int32_t mbedtls_TLS_FreeRTOS_recv( NetworkContext_t *, void *, size_t );
extern int32_t mbedtls_TLS_FreeRTOS_send( NetworkContext_t *, const void *, size_t );
extern size_t  mbedtls_TLS_FreeRTOS_params_size( void );

/* Allocate a zeroed params block of the given size and run one backend's Connect on it. */
static TlsTransportStatus_t tryBackend( size_t paramsSize,
                                        TlsTransportStatus_t ( * connect )( NetworkContext_t *, const char *,
                                                                            uint16_t, const NetworkCredentials_t *,
                                                                            uint32_t, uint32_t ),
                                        void ** outParams,
                                        const char * pHostName, uint16_t port,
                                        const NetworkCredentials_t * creds,
                                        uint32_t rxTmo, uint32_t txTmo )
{
    void * params = pvPortMalloc( paramsSize );

    *outParams = NULL;
    if( params == NULL )
    {
        return TLS_TRANSPORT_INSUFFICIENT_MEMORY;
    }
    ( void ) memset( params, 0, paramsSize );

    {
        struct NetworkContext beCtx;
        beCtx.pParams = ( TlsTransportParams_t * ) params;
        TlsTransportStatus_t st = connect( &beCtx, pHostName, port, creds, rxTmo, txTmo );

        if( st == TLS_TRANSPORT_SUCCESS )
        {
            *outParams = params;
        }
        else
        {
            vPortFree( params );
        }
        return st;
    }
}

TlsTransportStatus_t TLS_FreeRTOS_Connect( NetworkContext_t * pNetworkContext,
                                           const char * pHostName,
                                           uint16_t port,
                                           const NetworkCredentials_t * pNetworkCredentials,
                                           uint32_t receiveTimeoutMs,
                                           uint32_t sendTimeoutMs )
{
    TlsTransportParams_t * fp;
    TlsTransportStatus_t st;
    TlsBackendMode_t mode;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }
    fp = pNetworkContext->pParams;
    fp->backend = FB_BACKEND_NONE;
    fp->beParams = NULL;

    mode = ( pNetworkCredentials != NULL ) ? pNetworkCredentials->backendMode
                                           : TLS_BACKEND_MODE_FALLBACK;

    /* Forced mbedTLS (TLS 1.2): skip picotls entirely. */
    if( mode == TLS_BACKEND_MODE_MBEDTLS )
    {
        st = tryBackend( mbedtls_TLS_FreeRTOS_params_size(), mbedtls_TLS_FreeRTOS_Connect, &fp->beParams,
                         pHostName, port, pNetworkCredentials, receiveTimeoutMs, sendTimeoutMs );
        if( st == TLS_TRANSPORT_SUCCESS )
        {
            fp->backend = FB_BACKEND_MBEDTLS;
            LogInfo( ( "connected via mbedTLS (TLS 1.2, forced)" ) );
            return TLS_TRANSPORT_SUCCESS;
        }
        LogError( ( "forced mbedTLS connect failed (status=%d)", ( int ) st ) );
        return st;
    }

    /* 1) picotls — TLS 1.3 only (the default; smaller + shares the QUIC stack). Both FALLBACK and
     * forced-PICOTLS start here. */
    st = tryBackend( picotls_TLS_FreeRTOS_params_size(), picotls_TLS_FreeRTOS_Connect, &fp->beParams,
                     pHostName, port, pNetworkCredentials, receiveTimeoutMs, sendTimeoutMs );
    if( st == TLS_TRANSPORT_SUCCESS )
    {
        fp->backend = FB_BACKEND_PICOTLS;
        LogInfo( ( "connected via picotls (TLS 1.3)" ) );
        return TLS_TRANSPORT_SUCCESS;
    }

    /* Forced picotls: never fall back — return picotls's status verbatim so a TLS-1.3 server (AWS
     * IoT / S3) is never silently downgraded to TLS 1.2 by a transient handshake failure. */
    if( mode == TLS_BACKEND_MODE_PICOTLS )
    {
        LogError( ( "forced picotls handshake failed (status=%d) -- no fallback", ( int ) st ) );
        return st;
    }

    /* FALLBACK: only fall back on a handshake failure (the symptom of a server with no TLS 1.3). A
     * bad parameter, TCP-connect failure or out-of-memory would fail mbedTLS the same way — don't
     * waste a second round trip. */
    if( st != TLS_TRANSPORT_HANDSHAKE_FAILED )
    {
        LogError( ( "picotls connect failed (status=%d), not a TLS-version issue -- no fallback", ( int ) st ) );
        return st;
    }

    LogWarn( ( "picotls (TLS 1.3) handshake failed -- falling back to mbedTLS (TLS 1.2/1.3)" ) );

    /* 2) mbedTLS — negotiates TLS 1.2 or 1.3. */
    st = tryBackend( mbedtls_TLS_FreeRTOS_params_size(), mbedtls_TLS_FreeRTOS_Connect, &fp->beParams,
                     pHostName, port, pNetworkCredentials, receiveTimeoutMs, sendTimeoutMs );
    if( st == TLS_TRANSPORT_SUCCESS )
    {
        fp->backend = FB_BACKEND_MBEDTLS;
        LogInfo( ( "connected via mbedTLS (TLS 1.2/1.3 fallback)" ) );
        return TLS_TRANSPORT_SUCCESS;
    }

    LogError( ( "mbedTLS fallback also failed (status=%d)", ( int ) st ) );
    return st;
}

void TLS_FreeRTOS_Disconnect( NetworkContext_t * pNetworkContext )
{
    TlsTransportParams_t * fp;
    struct NetworkContext beCtx;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        return;
    }
    fp = pNetworkContext->pParams;
    if( fp->beParams == NULL )
    {
        return;
    }

    beCtx.pParams = ( TlsTransportParams_t * ) fp->beParams;
    if( fp->backend == FB_BACKEND_PICOTLS )
    {
        picotls_TLS_FreeRTOS_Disconnect( &beCtx );
    }
    else if( fp->backend == FB_BACKEND_MBEDTLS )
    {
        mbedtls_TLS_FreeRTOS_Disconnect( &beCtx );
    }

    vPortFree( fp->beParams );
    fp->beParams = NULL;
    fp->backend = FB_BACKEND_NONE;
}

int32_t TLS_FreeRTOS_send( NetworkContext_t * pNetworkContext,
                           const void * pBuffer,
                           size_t bytesToSend )
{
    TlsTransportParams_t * fp;
    struct NetworkContext beCtx;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        return -1;
    }
    fp = pNetworkContext->pParams;
    if( fp->beParams == NULL )
    {
        return -1;
    }

    beCtx.pParams = ( TlsTransportParams_t * ) fp->beParams;
    if( fp->backend == FB_BACKEND_PICOTLS )
    {
        return picotls_TLS_FreeRTOS_send( &beCtx, pBuffer, bytesToSend );
    }
    if( fp->backend == FB_BACKEND_MBEDTLS )
    {
        return mbedtls_TLS_FreeRTOS_send( &beCtx, pBuffer, bytesToSend );
    }
    return -1;
}

int32_t TLS_FreeRTOS_recv( NetworkContext_t * pNetworkContext,
                           void * pBuffer,
                           size_t bytesToRecv )
{
    TlsTransportParams_t * fp;
    struct NetworkContext beCtx;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        return -1;
    }
    fp = pNetworkContext->pParams;
    if( fp->beParams == NULL )
    {
        return -1;
    }

    beCtx.pParams = ( TlsTransportParams_t * ) fp->beParams;
    if( fp->backend == FB_BACKEND_PICOTLS )
    {
        return picotls_TLS_FreeRTOS_recv( &beCtx, pBuffer, bytesToRecv );
    }
    if( fp->backend == FB_BACKEND_MBEDTLS )
    {
        return mbedtls_TLS_FreeRTOS_recv( &beCtx, pBuffer, bytesToRecv );
    }
    return -1;
}
