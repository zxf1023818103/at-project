/*
 * picotls TLS transport — a drop-in alternative to transport_mbedtls.h.
 *
 * Exposes the SAME surface as the mbedTLS transport (TLS_FreeRTOS_Connect/Disconnect/
 * send/recv, NetworkCredentials_t, NetworkContext_t, TlsTransportStatus_t), so coreMQTT /
 * coreHTTP and the application are unchanged. The backend is picked at link time via the
 * neutral selector transport_tls.h (the linked network_transport_* library defines
 * TLS_BACKEND_PICOTLS, or not). See doc / the plan for the switch mechanism.
 *
 * picotls is TLS 1.3 only. It drives TLS over the shared TCP_Sockets_* layer directly
 * (no mbedTLS BIO — picotls is buffer based). mbedTLS is used only for crypto primitives
 * (via picotls's ptls_mbedtls backend), for X.509 server-cert verification, and for
 * client-key signing — see transport_picotls.c.
 */

#ifndef USING_PICOTLS
#define USING_PICOTLS

/* picotls core: ptls_t, ptls_buffer_t (TlsTransportParams_t embeds the latter by value). */
#include "picotls.h"

/* TCP Sockets Wrapper (Socket_t, BaseType_t) and the coreMQTT transport interface. */
#include "tcp_sockets_wrapper.h"
#include "transport_interface.h"

/* Backend-agnostic NetworkCredentials_t / TlsTransportStatus_t (shared with the mbedTLS backend
 * and the fallback dispatcher). */
#include "transport_tls_common.h"

/* For the TLS-1.2 fallback build, both backends are linked side by side, so this backend's public
 * API symbols are renamed (the network_transport CMake defines TLS_TRANSPORT_RENAME_PICOTLS) and
 * the fallback dispatcher (transport_fallback.c) calls the picotls_* set directly. */
#ifdef TLS_TRANSPORT_RENAME_PICOTLS
    #define TLS_FreeRTOS_Connect      picotls_TLS_FreeRTOS_Connect
    #define TLS_FreeRTOS_Disconnect   picotls_TLS_FreeRTOS_Disconnect
    #define TLS_FreeRTOS_send         picotls_TLS_FreeRTOS_send
    #define TLS_FreeRTOS_recv         picotls_TLS_FreeRTOS_recv
    #define TLS_FreeRTOS_params_size  picotls_TLS_FreeRTOS_params_size
#endif

/**
 * @brief Parameters for the network context of the transport interface
 * implementation that uses picotls and FreeRTOS+TCP sockets.
 *
 * One TLS connection at a time (the AWS demo reconnects sequentially): the heavyweight
 * picotls context + the X.509 verifier / client signer / parsed cert chains are
 * file-scope static in transport_picotls.c, so this stays small (the app embeds it by
 * value, mirroring the mbedTLS transport).
 */
typedef struct TlsTransportParams
{
    Socket_t      tcpSocket;
    ptls_t      * tls;        /**< @brief picotls connection (ptls_client_new). */
    ptls_buffer_t rxPlain;    /**< @brief Decrypted-plaintext sink for recv (grows on demand). */
    size_t        rxConsumed; /**< @brief Bytes of rxPlain already returned to the caller. */
    /* Record-framing recv (transport_picotls.c): a reused per-connection buffer that accumulates ONE
     * whole TLS record before feeding it to ptls_receive, so upstream picotls takes its zero-alloc
     * fast path (no per-record recvbuf.rec malloc/free). Without this, a long download on a fragmented
     * no-PSRAM heap eventually can't malloc a 16 KB record -> PTLS_ERROR_NO_MEMORY. */
    uint8_t     * rxFrame;      /**< @brief Reused one-record assembly buffer (PICOTLS_MAX_RECORD, heap). */
    size_t        rxFrameFilled;/**< @brief Bytes of the current record already read into rxFrame. */
    size_t        rxFrameNeed;  /**< @brief Total bytes of the current record (0 until the header is in). */
} TlsTransportParams_t;

/* NetworkCredentials_t and TlsTransportStatus_t are now in transport_tls_common.h (included above). */

/**
 * @brief Create a TLS connection with FreeRTOS sockets.
 *
 * @param[out] pNetworkContext Pointer to a network context to contain the
 * initialized socket handle.
 * @param[in] pHostName The hostname of the remote endpoint.
 * @param[in] port The destination port.
 * @param[in] pNetworkCredentials Credentials for the TLS connection.
 * @param[in] receiveTimeoutMs Receive socket timeout.
 * @param[in] sendTimeoutMs Send socket timeout.
 *
 * @return #TLS_TRANSPORT_SUCCESS on success; an error status otherwise.
 */
TlsTransportStatus_t TLS_FreeRTOS_Connect( NetworkContext_t * pNetworkContext,
                                           const char * pHostName,
                                           uint16_t port,
                                           const NetworkCredentials_t * pNetworkCredentials,
                                           uint32_t receiveTimeoutMs,
                                           uint32_t sendTimeoutMs );

/**
 * @brief Gracefully disconnect an established TLS connection.
 *
 * @param[in] pNetworkContext Network context.
 */
void TLS_FreeRTOS_Disconnect( NetworkContext_t * pNetworkContext );

/**
 * @brief Receives data from an established TLS connection (TransportRecv_t).
 *
 * @return Number of bytes (> 0) received; 0 if the socket times out without data;
 * a negative value on error.
 */
int32_t TLS_FreeRTOS_recv( NetworkContext_t * pNetworkContext,
                           void * pBuffer,
                           size_t bytesToRecv );

/**
 * @brief Sends data over an established TLS connection (TransportSend_t).
 *
 * @return Number of bytes (> 0) sent; 0 if the socket times out before any bytes are
 * sent; a negative value on error.
 */
int32_t TLS_FreeRTOS_send( NetworkContext_t * pNetworkContext,
                           const void * pBuffer,
                           size_t bytesToSend );

/**
 * @brief sizeof(TlsTransportParams_t) for this backend, so the fallback dispatcher can allocate a
 * params block of the right size without seeing the (backend-private) struct layout.
 */
size_t TLS_FreeRTOS_params_size( void );

#endif /* ifndef USING_PICOTLS */
