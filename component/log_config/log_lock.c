/*
 * log_lock.c — thread-safety for rxi/log.c (submodule component/log_config/log).
 *
 * log.c is not thread-safe on its own: log_log() walks the callback list and writes the line
 * with no serialization, so two tasks logging at once can interleave their output (and race the
 * callback list). log.c exposes log_set_lock(fn, udata): it calls fn(true, udata) to acquire and
 * fn(false, udata) to release around each log operation. We back that with a FreeRTOS mutex.
 *
 * The default output (fprintf -> stderr) is intentionally left unchanged; this file only adds the
 * lock. Call log_lock_init() once at startup, before a second task can log. Task context only —
 * the mutex take/give must not run from an ISR.
 */
#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "log.h"          /* log_set_lock / log_LockFn */
#include "log_lock.h"

/* Static (no heap): configSUPPORT_STATIC_ALLOCATION == 1 (enabled for all platforms in
 * FreeRTOSConfig.h). FreeRTOS objects can be created before the scheduler, so log_lock_init()
 * at main() start is fine. */
static StaticSemaphore_t s_log_mutex_buf;
static SemaphoreHandle_t s_log_mutex;

/* log_LockFn (log.h): lock=true -> acquire, lock=false -> release. */
static void log_lock_cb( bool lock, void * udata )
{
    SemaphoreHandle_t mutex = ( SemaphoreHandle_t ) udata;

    if( lock )
    {
        ( void ) xSemaphoreTake( mutex, portMAX_DELAY );
    }
    else
    {
        ( void ) xSemaphoreGive( mutex );
    }
}

void log_lock_init( void )
{
    if( s_log_mutex == NULL )
    {
        s_log_mutex = xSemaphoreCreateMutexStatic( &s_log_mutex_buf );
        log_set_lock( log_lock_cb, s_log_mutex );
    }
}
