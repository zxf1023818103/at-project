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
} TlsTransportParams_t;

/**
 * @brief Contains the credentials necessary for tls connection setup.
 *
 * Identical layout to the mbedTLS transport's NetworkCredentials_t (backend-agnostic),
 * so the application fills it the same way regardless of which backend is linked.
 */
typedef struct NetworkCredentials
{
    /**
     * @brief To use ALPN, set this to a NULL-terminated list of supported
     * protocols in decreasing order of preference. (Not wired on the picotls path
     * yet — AWS IoT on port 8883 needs none; a non-NULL value is logged and ignored.)
     */
    const char ** pAlpnProtos;

    /**
     * @brief Disable server name indication (SNI) for a TLS session.
     */
    BaseType_t disableSni;

    const uint8_t * pRootCa;     /**< @brief Trusted server root certificate (PEM or DER). */
    size_t rootCaSize;           /**< @brief Size associated with #NetworkCredentials.pRootCa. */
    const uint8_t * pClientCert; /**< @brief Client certificate (PEM or DER), or NULL. */
    size_t clientCertSize;       /**< @brief Size associated with #NetworkCredentials.pClientCert. */
    const uint8_t * pPrivateKey; /**< @brief Client private key (PEM or DER), or NULL. */
    size_t privateKeySize;       /**< @brief Size associated with #NetworkCredentials.pPrivateKey. */

    /**
     * @brief Filesystem path of the client certificate (PEM or DER), loaded when
     * #pClientCert is NULL. Lets a non-secret cert live as a plain file while the key
     * stays in secure storage.
     */
    const char * pClientCertPath;

    /**
     * @brief Optional override for the rxPlain pre-reserve size (bytes). 0 = the
     * default full-TLS-record reserve (16640). A second concurrent TLS connection
     * (e.g. the OTA HTTPS download alongside the AWS MQTT session) can pass a smaller
     * value to save heap: its application records are tiny (4 KB OTA blocks), so a
     * modest reserve (e.g. 6144) avoids holding a second 16 KB buffer. Only consumed
     * by the picotls backend; ignored by mbedTLS.
     */
    size_t rxReserveSize;
} NetworkCredentials_t;

/**
 * @brief TLS Connect / Disconnect return status.
 */
typedef enum TlsTransportStatus
{
    TLS_TRANSPORT_SUCCESS = 0,         /**< Function successfully completed. */
    TLS_TRANSPORT_INVALID_PARAMETER,   /**< At least one parameter was invalid. */
    TLS_TRANSPORT_INSUFFICIENT_MEMORY, /**< Insufficient memory required to establish connection. */
    TLS_TRANSPORT_INVALID_CREDENTIALS, /**< Provided credentials were invalid. */
    TLS_TRANSPORT_HANDSHAKE_FAILED,    /**< Performing TLS handshake with server failed. */
    TLS_TRANSPORT_INTERNAL_ERROR,      /**< A call to a system API resulted in an internal error. */
    TLS_TRANSPORT_CONNECT_FAILURE      /**< Initial connection to the server failed. */
} TlsTransportStatus_t;

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

#endif /* ifndef USING_PICOTLS */
