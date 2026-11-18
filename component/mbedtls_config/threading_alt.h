/*
 * threading_alt.h — MBEDTLS_THREADING_ALT mutex type (FreeRTOS, platform-agnostic).
 *
 * mbedtls/threading.h #includes this (bare name, found on the include path a platform port
 * adds — e.g. mbedtls_cfg) when MBEDTLS_THREADING_ALT is set. It must define
 * mbedtls_threading_mutex_t; the callbacks live in mbedtls_threading_alt.c (this dir).
 */
#ifndef MBEDTLS_THREADING_ALT_H
#define MBEDTLS_THREADING_ALT_H

/* Opaque mutex handle: stored as void* (a FreeRTOS SemaphoreHandle_t is a pointer) so the
 * mbedTLS sources that instantiate this struct do NOT have to pull FreeRTOS/SDK headers.
 * mbedtls_threading_alt.c casts to/from SemaphoreHandle_t. */
typedef struct mbedtls_threading_mutex_t
{
    void *mutex;
    char  is_valid;   /* 0 until init succeeds; guards double-free / use-before-init */
} mbedtls_threading_mutex_t;

#endif /* MBEDTLS_THREADING_ALT_H */
