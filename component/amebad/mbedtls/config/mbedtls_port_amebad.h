/*
 * AmebaD platform memory/IO header for Mbed TLS.
 *
 * config-tls-generic.h sets MBEDTLS_PLATFORM_NO_STD_FUNCTIONS +
 * MBEDTLS_PLATFORM_STD_MEM_HDR "mbedtls_port_amebad.h", so Mbed TLS pulls
 * calloc/free/printf from here instead of assuming a hosted libc by default.
 * KM4 image2 links newlib (-lc), so these map straight to the C library.
 * (HW-accelerated crypto / Realtek TRNG entropy / net_sockets are NOT wired
 * here — they come in as separate port pieces when TLS is actually consumed.)
 */
#ifndef MBEDTLS_PORT_PLATFORM_AMEBAD_H
#define MBEDTLS_PORT_PLATFORM_AMEBAD_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MBEDTLS_PLATFORM_STD_FREE       free
#define MBEDTLS_PLATFORM_STD_CALLOC     calloc

#define MBEDTLS_PLATFORM_FPRINTF_MACRO  fprintf
#define MBEDTLS_PLATFORM_PRINTF_MACRO   printf
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO snprintf
#define MBEDTLS_PLATFORM_EXIT_MACRO     exit

#define MBEDTLS_PLATFORM_MS_TIME_TYPE_MACRO uint64_t

#endif /* MBEDTLS_PORT_PLATFORM_AMEBAD_H */
