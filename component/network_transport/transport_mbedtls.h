/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/**
 * @file tls_freertos.h
 * @brief TLS transport interface header.
 */

#ifndef USING_MBEDTLS
#define USING_MBEDTLS

/* MBed TLS includes. */
#if !defined( MBEDTLS_CONFIG_FILE )
    #include "mbedtls/mbedtls_config.h"
#else
    #include MBEDTLS_CONFIG_FILE
#endif

#include "mbedtls/build_info.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509.h"
#include "mbedtls/error.h"


/**************************************************/
/******* DO NOT CHANGE the following order ********/
/**************************************************/

/* Logging related header files are required to be included in the following order:
 * 1. Include the header file "logging_levels.h".
 * 2. Define LIBRARY_LOG_NAME and  LIBRARY_LOG_LEVEL.
 * 3. Include the header file "logging_stack.h".
 */

/* Include header that defines log levels. */
#include "logging_levels.h"

/* Logging configuration for the Sockets. */
#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME     "TlsTransport"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_ERROR
#endif

/** @brief Prototype for the function used to print to console on Windows
 * simulator of FreeRTOS.
 *
 * @note The function prints to the console before the network is connected;
 * then a UDP port after the network has connected. */
extern void vLoggingPrintf( const char * pcFormatString,
                            ... );

/* Map the SdkLog macro to the logging function to enable logging
 * on Windows simulator. */
#ifndef SdkLog
    #define SdkLog( message )    vLoggingPrintf message
#endif

#include "logging_stack.h"

/************ End of logging configuration ****************/

/* TCP Sockets Wrapper include.*/
#include "tcp_sockets_wrapper.h"

/* Transport interface include. */
#include "transport_interface.h"

/* Backend-agnostic NetworkCredentials_t / TlsTransportStatus_t (shared with the picotls backend
 * and the fallback dispatcher). */
#include "transport_tls_common.h"

/* For the TLS-1.2 fallback build, both backends are linked side by side, so this backend's public
 * API symbols are renamed (the network_transport CMake defines TLS_TRANSPORT_RENAME_MBEDTLS) and
 * the fallback dispatcher (transport_fallback.c) calls the mbedtls_* set directly. */
#ifdef TLS_TRANSPORT_RENAME_MBEDTLS
    #define TLS_FreeRTOS_Connect      mbedtls_TLS_FreeRTOS_Connect
    #define TLS_FreeRTOS_Disconnect   mbedtls_TLS_FreeRTOS_Disconnect
    #define TLS_FreeRTOS_send         mbedtls_TLS_FreeRTOS_send
    #define TLS_FreeRTOS_recv         mbedtls_TLS_FreeRTOS_recv
    #define TLS_FreeRTOS_params_size  mbedtls_TLS_FreeRTOS_params_size
#endif

/**
 * @brief Secured connection context.
 */
typedef struct SSLContext
{
    mbedtls_ssl_config config;               /**< @brief SSL connection configuration. */
    mbedtls_ssl_context context;             /**< @brief SSL connection context */
    mbedtls_x509_crt_profile certProfile;    /**< @brief Certificate security profile for this connection. */
    mbedtls_x509_crt rootCa;                 /**< @brief Root CA certificate context. */
    mbedtls_x509_crt clientCert;             /**< @brief Client certificate context. */
    mbedtls_pk_context privKey;              /**< @brief Client private key context. */
#if !defined( MBEDTLS_PSA_CRYPTO_C )
    /* Per-connection RNG. With PSA crypto present the TLS RNG draws from the shared global PSA DRBG
     * (transport_mbedtls.c TLS_RNG_FUNC) — the same one picotls uses — so these are omitted, saving
     * ~1 KB per connection and the per-connection entropy reseed. */
    mbedtls_entropy_context entropyContext;  /**< @brief Entropy context for random number generation. */
    mbedtls_ctr_drbg_context ctrDrbgContext; /**< @brief CTR DRBG context for random number generation. */
#endif
} SSLContext_t;

/**
 * @brief Parameters for the network context of the transport interface
 * implementation that uses mbedTLS and FreeRTOS+TCP sockets.
 */
typedef struct TlsTransportParams
{
    Socket_t tcpSocket;
    SSLContext_t sslContext;
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
 * @return #TLS_TRANSPORT_SUCCESS, #TLS_TRANSPORT_INSUFFICIENT_MEMORY, #TLS_TRANSPORT_INVALID_CREDENTIALS,
 * #TLS_TRANSPORT_HANDSHAKE_FAILED, #TLS_TRANSPORT_INTERNAL_ERROR, or #TLS_TRANSPORT_CONNECT_FAILURE.
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
 * @brief Receives data from an established TLS connection.
 *
 * @note This is the TLS version of the transport interface's
 * #TransportRecv_t function.
 *
 * @param[in] pNetworkContext The Network context.
 * @param[out] pBuffer Buffer to receive bytes into.
 * @param[in] bytesToRecv Number of bytes to receive from the network.
 *
 * @return Number of bytes (> 0) received if successful;
 * 0 if the socket times out without reading any bytes;
 * negative value on error.
 */
int32_t TLS_FreeRTOS_recv( NetworkContext_t * pNetworkContext,
                           void * pBuffer,
                           size_t bytesToRecv );

/**
 * @brief Sends data over an established TLS connection.
 *
 * @note This is the TLS version of the transport interface's
 * #TransportSend_t function.
 *
 * @param[in] pNetworkContext The network context.
 * @param[in] pBuffer Buffer containing the bytes to send.
 * @param[in] bytesToSend Number of bytes to send from the buffer.
 *
 * @return Number of bytes (> 0) sent on success;
 * 0 if the socket times out without sending any bytes;
 * else a negative value to represent error.
 */
int32_t TLS_FreeRTOS_send( NetworkContext_t * pNetworkContext,
                           const void * pBuffer,
                           size_t bytesToSend );

/**
 * @brief sizeof(TlsTransportParams_t) for this backend, so the fallback dispatcher can allocate a
 * params block of the right size without seeing the (backend-private) struct layout.
 */
size_t TLS_FreeRTOS_params_size( void );


#ifdef MBEDTLS_DEBUG_C

/**
 * @brief Write an MBedTLS Debug message to the LogDebug() function
 *
 * @param[in] sslContext Pointer of the SSL Context that is being used
 * @param[in] level The severity level of the debug message from MBedTLS
 * @param[in] file Name of the file that the debug message is from
 * @param[in] line The line number that the debug message is from
 * @param[in] str The full string debug message from MBedTLS
 *
 * @return void
 */
    void mbedtls_string_printf( void * sslContext,
                                int level,
                                const char * file,
                                int line,
                                const char * str );
#endif /* MBEDTLS_DEBUG_C */

#endif /* ifndef USING_MBEDTLS */
