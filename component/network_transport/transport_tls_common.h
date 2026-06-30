/*
 * transport_tls_common.h — the backend-agnostic TLS transport types shared by the picotls
 * (transport_picotls.h) and mbedTLS (transport_mbedtls.h) backends and the TLS-1.2 fallback
 * dispatcher (transport_fallback.h).
 *
 * NetworkCredentials_t and TlsTransportStatus_t are identical across backends, so they live here
 * (one definition) — this is what lets the fallback dispatcher include all three without a struct
 * / enum redefinition clash. Each backend keeps its own TlsTransportParams_t (different internal
 * state) in its own header.
 */
#ifndef TRANSPORT_TLS_COMMON_H
#define TRANSPORT_TLS_COMMON_H

#include <stdint.h>
#include <stddef.h>

#include "FreeRTOS.h"   /* BaseType_t */

/**
 * @brief Per-connection TLS backend selection. Honored only by the fallback dispatcher
 * (transport_fallback.c), which carries BOTH backends; single-backend builds (picotls-only /
 * mbedTLS-only) compile this but ignore it (the one linked backend is always used). 0 = default.
 */
typedef enum TlsBackendMode
{
    TLS_BACKEND_MODE_FALLBACK = 0, /**< picotls (TLS 1.3) first, then mbedTLS (TLS 1.2) — default. */
    TLS_BACKEND_MODE_PICOTLS  = 1, /**< Force picotls (TLS 1.3); no fallback to mbedTLS. */
    TLS_BACKEND_MODE_MBEDTLS  = 2  /**< Force mbedTLS (TLS 1.2 in the fallback build). */
} TlsBackendMode_t;

/**
 * @brief Credentials for a TLS connection. Backend-agnostic: the application fills this the same
 * way regardless of which backend (picotls / mbedTLS / fallback) is linked.
 */
typedef struct NetworkCredentials
{
    /** @brief NULL-terminated ALPN protocol list (decreasing preference), or NULL. */
    const char ** pAlpnProtos;

    /** @brief Disable server name indication (SNI) for a TLS session. */
    BaseType_t disableSni;

    const uint8_t * pRootCa;     /**< @brief Trusted server root certificate (PEM or DER). */
    size_t rootCaSize;           /**< @brief Size associated with #pRootCa. */
    const uint8_t * pClientCert; /**< @brief Client certificate (PEM or DER), or NULL. */
    size_t clientCertSize;       /**< @brief Size associated with #pClientCert. */
    const uint8_t * pPrivateKey; /**< @brief Client private key (PEM or DER), or NULL. */
    size_t privateKeySize;       /**< @brief Size associated with #pPrivateKey. */

    /** @brief Filesystem path of the client certificate, loaded when #pClientCert is NULL. */
    const char * pClientCertPath;

    /** @brief picotls rxPlain pre-reserve override (bytes); 0 = default. Ignored by mbedTLS. */
    size_t rxReserveSize;

    /** @brief Backend to use for this connection. Honored only by the fallback dispatcher
     * (transport_fallback.c); 0 = TLS_BACKEND_MODE_FALLBACK. See #TlsBackendMode_t. */
    TlsBackendMode_t backendMode;

    /** @brief Filesystem path of a trusted-root CA folder (or single PEM/DER file). See the
     * per-backend headers; the `ca/` store + (CRT_BUNDLE=ON) the embedded Mozilla bundle. */
    const char * pRootCaPath;
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

#endif /* TRANSPORT_TLS_COMMON_H */
