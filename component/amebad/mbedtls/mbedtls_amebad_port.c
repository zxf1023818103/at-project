/*
 * mbedtls_amebad_port.c — AmebaD runtime glue for upstream Mbed TLS v3.6.4:
 *   (1) MBEDTLS_THREADING_ALT  : FreeRTOS-mutex implementation of the 4 threading callbacks,
 *       registered by amebad_mbedtls_port_init() (call ONCE before any mbedTLS use).
 *   (2) MBEDTLS_ENTROPY_HARDWARE_ALT : mbedtls_hardware_poll() entropy source.
 *
 * NOTE on entropy: this mirrors the wolfcrypt_glue.c stance — a fast xorshift, NOT
 * crypto-grade. It exists so CTR_DRBG/keygen have *a* source; a real TRNG (RTL8721D TRNG /
 * efuse seed) is the follow-up. Do not ship secrets generated from this as-is.
 */
#include <stdint.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "mbedtls/threading.h"
#include "mbedtls/error.h"

/* ---- (1) threading: FreeRTOS mutex over mbedtls_threading_mutex_t (threading_alt.h) ---- */

static void amebad_mtx_init( mbedtls_threading_mutex_t *m )
{
    if( m == NULL )
    {
        return;
    }
    /* RECURSIVE: PSA crypto (picotls's all-PSA backend) can take the same mbedTLS
     * mutex nested on one call path; a plain mutex would self-deadlock. */
    m->mutex    = xSemaphoreCreateRecursiveMutex();
    m->is_valid = ( m->mutex != NULL ) ? 1 : 0;
}

static void amebad_mtx_free( mbedtls_threading_mutex_t *m )
{
    if( m != NULL && m->is_valid )
    {
        vSemaphoreDelete( m->mutex );
        m->is_valid = 0;
    }
}

static int amebad_mtx_lock( mbedtls_threading_mutex_t *m )
{
    if( m == NULL || !m->is_valid )
    {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    return ( xSemaphoreTakeRecursive( m->mutex, portMAX_DELAY ) == pdTRUE )
           ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}

static int amebad_mtx_unlock( mbedtls_threading_mutex_t *m )
{
    if( m == NULL || !m->is_valid )
    {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    return ( xSemaphoreGiveRecursive( m->mutex ) == pdTRUE )
           ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}

/* Register the FreeRTOS threading callbacks. Call once, before the scheduler starts using
 * mbedTLS (and before corePKCS11's C_Initialize). */
void amebad_mbedtls_port_init( void )
{
    mbedtls_threading_set_alt( amebad_mtx_init, amebad_mtx_free,
                               amebad_mtx_lock, amebad_mtx_unlock );
}

/* ---- (2) entropy: MBEDTLS_ENTROPY_HARDWARE_ALT hook ---------------------------------- */

int mbedtls_hardware_poll( void *data, unsigned char *output, size_t len, size_t *olen )
{
    static uint32_t s = 0x9E3779B9u;   /* seeded; per-boot variation is a TRNG follow-up */
    size_t i;

    ( void ) data;
    for( i = 0; i < len; i++ )
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        output[ i ] = ( unsigned char ) ( s & 0xFFu );
    }
    if( olen != NULL )
    {
        *olen = len;
    }
    return 0;
}
