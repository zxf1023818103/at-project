/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
void *tl3238_nimble_calloc(size_t count, size_t size);
void tl3238_nimble_free(void *ptr);
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO tl3238_nimble_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO tl3238_nimble_free
#define MBEDTLS_AES_C
/* Keep immutable AES tables in flash, freeing RAM for paired RAS records. */
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CMAC_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_BIGNUM_C
#ifdef TL3238_ENABLE_OTA
#define MBEDTLS_SHA256_C
#endif
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_MPI_MAX_SIZE 32
/* NimBLE supplies HCI LE Rand to ECDH; no OS entropy or TLS stack needed. */
