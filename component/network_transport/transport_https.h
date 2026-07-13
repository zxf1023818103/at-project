/*
 * transport_https.h — the shared TLS-connect surface used by EVERY AWS/HTTPS connection:
 * the AWS IoT MQTT client (aws_connect.c), the AWS OTA S3 data plane (ota_app.c), and the
 * ota_http command (ota_http.c). One place for the server-trust config + one RUNTIME-selectable
 * TLS backend (mbedTLS / picotls / fallback) shared across all three.
 *
 * Https_Connect() is server-only trust (no client cert): the OTA / ota_http downloads. The AWS
 * MQTT path (mutual TLS, client cert + key) fills its own NetworkCredentials_t in aws_connect.c
 * but takes the SAME runtime backend via at_tls_get_backend().
 */
#ifndef TRANSPORT_HTTPS_H
#define TRANSPORT_HTTPS_H

#include <stdint.h>
#include <stddef.h>

#include "transport_tls.h"   /* NetworkContext_t (via the linked backend header) + TlsTransportStatus_t + TlsBackendMode_t */

/**
 * @brief The process-wide TLS backend for every AWS/HTTPS connection. Only a `fallback` build
 * (both backends linked, transport_fallback.c dispatches) can actually switch at runtime; a
 * single-backend build (picotls-only / mbedTLS-only) ignores it and always uses the one linked.
 * Default TLS_BACKEND_MODE_FALLBACK (picotls/TLS1.3 first, then mbedTLS/TLS1.2).
 */
void             at_tls_set_backend( TlsBackendMode_t mode );
TlsBackendMode_t at_tls_get_backend( void );

/** Parse "fallback" / "picotls" / "mbedtls" -> mode (returns FALLBACK on anything else). */
TlsBackendMode_t at_tls_backend_from_str( const char * s );

/**
 * @brief Connect to a public HTTPS server, server-only trust (no client cert). Verifies the chain
 * via the embedded Mozilla CRT bundle (AT_PROJECT_CRT_BUNDLE builds) or Amazon Root CA 1, using
 * the runtime backend (at_tls_get_backend()). Caller owns @p pNet and its TlsTransportParams_t.
 */
TlsTransportStatus_t Https_Connect( NetworkContext_t * pNet, const char * host, uint16_t port,
                                    size_t rxReserve, uint32_t recvTmoMs, uint32_t sendTmoMs );

#endif /* TRANSPORT_HTTPS_H */
