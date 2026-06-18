/*
 * mbedtls_threading_alt.c — generic MBEDTLS_THREADING_ALT implementation over FreeRTOS.
 *
 * Provides the four threading callbacks (mbedtls_threading_mutex_t from threading_alt.h,
 * recursive FreeRTOS mutexes) and registers them via mbedtls_threading_freertos_init(),
 * which must be called ONCE before any mbedTLS use (and before corePKCS11's C_Initialize).
 *
 * Platform-agnostic (any FreeRTOS target using upstream mbedTLS), so it lives in the shared
 * mbedtls_config component; the platform-specific entropy source is each port's own
 * (e.g. component/amebad/mbedtls_port/entropy_hardware_alt.c).
 */
#include "FreeRTOS.h"
#include "semphr.h"

#include "mbedtls/threading.h"
#include "mbedtls/error.h"

static void freertos_mtx_init( mbedtls_threading_mutex_t *m )
{
    if( m == NULL )
    {
        return;
    }
    /* RECURSIVE: PSA crypto (picotls's all-PSA backend) can take the same mbedTLS mutex
     * nested on one call path; a plain mutex would self-deadlock. */
    m->mutex    = xSemaphoreCreateRecursiveMutex();
    m->is_valid = ( m->mutex != NULL ) ? 1 : 0;
}

static void freertos_mtx_free( mbedtls_threading_mutex_t *m )
{
    if( m != NULL && m->is_valid )
    {
        vSemaphoreDelete( m->mutex );
        m->is_valid = 0;
    }
}

static int freertos_mtx_lock( mbedtls_threading_mutex_t *m )
{
    if( m == NULL || !m->is_valid )
    {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    return ( xSemaphoreTakeRecursive( m->mutex, portMAX_DELAY ) == pdTRUE )
           ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}

static int freertos_mtx_unlock( mbedtls_threading_mutex_t *m )
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
void mbedtls_threading_freertos_init( void )
{
    mbedtls_threading_set_alt( freertos_mtx_init, freertos_mtx_free,
                               freertos_mtx_lock, freertos_mtx_unlock );
}
