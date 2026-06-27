/*
 * Neutral TLS transport selector.
 *
 * Both TLS backends export the same TLS_FreeRTOS_* API and the same
 * NetworkCredentials_t / NetworkContext_t machinery, so application code includes THIS
 * header (not a backend-specific one) and the build flag picks the implementation:
 *
 *   network_transport_picotls  defines TLS_BACKEND_PICOTLS (PUBLIC) -> picotls path
 *   network_transport_mbedtls  defines nothing                       -> mbedTLS path
 *
 * The flag is threaded from project/amebad/Makefile (TLS_BACKEND=mbedtls|picotls) through
 * the AWS block in project/amebad/app_km4/CMakeLists.txt, which links exactly one of the
 * two libraries. Default is mbedTLS.
 */

#ifndef TRANSPORT_TLS_H
#define TRANSPORT_TLS_H

#if defined( TLS_BACKEND_PICOTLS )
    #include "transport_picotls.h"
#else
    #include "transport_mbedtls.h"
#endif

#endif /* TRANSPORT_TLS_H */
