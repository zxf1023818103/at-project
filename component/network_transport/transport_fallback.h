/*
 * transport_fallback.h — TLS transport with a TLS-1.2 fallback.
 *
 * Exposes the SAME TLS_FreeRTOS_* API + NetworkCredentials_t as the single-backend transports, so
 * the application (via transport_tls.h) is unchanged. Internally it tries the picotls backend
 * first (TLS 1.3 only — smaller, the project default); if the server doesn't speak TLS 1.3 (the
 * picotls handshake fails), it transparently falls back to the mbedTLS backend, which negotiates
 * TLS 1.2 or 1.3. Both backends are linked side by side with renamed symbols (picotls_TLS_*,
 * mbedtls_TLS_*) — see transport_fallback.c + the network_transport CMake (TLS_BACKEND=fallback).
 */
#ifndef TRANSPORT_FALLBACK_H
#define TRANSPORT_FALLBACK_H

#include "tcp_sockets_wrapper.h"    /* Socket_t */
#include "transport_interface.h"    /* NetworkContext_t */
#include "transport_tls_common.h"   /* NetworkCredentials_t, TlsTransportStatus_t */

/**
 * @brief Fallback transport params: which backend won the handshake + a heap block holding that
 * backend's own TlsTransportParams_t (allocated by Connect, sized via the backend's
 * TLS_FreeRTOS_params_size()). Small by value, so the app embeds it like any backend's params.
 */
typedef struct TlsTransportParams
{
    int    backend;   /**< 0 = none, 1 = picotls (TLS 1.3), 2 = mbedTLS (TLS 1.2/1.3 fallback). */
    void * beParams;  /**< Heap block: the winning backend's TlsTransportParams_t. */
} TlsTransportParams_t;

TlsTransportStatus_t TLS_FreeRTOS_Connect( NetworkContext_t * pNetworkContext,
                                           const char * pHostName,
                                           uint16_t port,
                                           const NetworkCredentials_t * pNetworkCredentials,
                                           uint32_t receiveTimeoutMs,
                                           uint32_t sendTimeoutMs );

void TLS_FreeRTOS_Disconnect( NetworkContext_t * pNetworkContext );

int32_t TLS_FreeRTOS_recv( NetworkContext_t * pNetworkContext,
                           void * pBuffer,
                           size_t bytesToRecv );

int32_t TLS_FreeRTOS_send( NetworkContext_t * pNetworkContext,
                           const void * pBuffer,
                           size_t bytesToSend );

#endif /* TRANSPORT_FALLBACK_H */
