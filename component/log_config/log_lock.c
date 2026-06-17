/*
 * log_lock.c — thread-safety for rxi/log.c (submodule component/log_config/log).
 *
 * log.c is not thread-safe on its own: log_log() walks the callback list and writes the line
 * with no serialization, so two tasks logging at once can interleave their output (and race the
 * callback list). log.c exposes log_set_lock(fn, udata): it calls fn(true, udata) to acquire and
 * fn(false, udata) to release around each log operation. We back that with a FreeRTOS mutex.
 *
 * Output stays fprintf -> stderr, but log_lock_init() reformats it: log.c's built-in line carries
 * its own "HH:MM:SS", which doubles up with the console's "[HH:MM:SS.mmm]" prefix, so we drop
 * log.c's timestamp (see log_cb_notime). Call log_lock_init() once at startup, before a second
 * task can log. Task context only — the mutex take/give must not run from an ISR.
 */
#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "log.h"          /* log_set_lock / log_LockFn / log_add_callback / log_Event */
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

/* log.c output callback WITHOUT its "HH:MM:SS" — prints "LEVEL file:line: msg" (log.c's default
 * minus the leading timestamp). See log_lock_init. */
static void log_cb_notime( log_Event * ev )
{
    FILE * fp = ( FILE * ) ev->udata;

    fprintf( fp, "%-5s %s:%d: ", log_level_string( ev->level ), ev->file, ev->line );
    vfprintf( fp, ev->fmt, ev->ap );
    fprintf( fp, "\n" );
}

void log_lock_init( void )
{
    if( s_log_mutex == NULL )
    {
        s_log_mutex = xSemaphoreCreateMutexStatic( &s_log_mutex_buf );
        log_set_lock( log_lock_cb, s_log_mutex );

        /* Drop log.c's own "HH:MM:SS": the cross-platform console (at_console_write) already
         * prefixes every line with "[HH:MM:SS.mmm]" (ms, plus an uptime "[+s.mmm]" fallback before
         * SNTP sync) — log.c's date-less clock would just be a redundant second timestamp. Silence
         * the built-in stderr sink and re-add one that emits only "LEVEL file:line: msg". */
        log_set_quiet( true );
        ( void ) log_add_callback( log_cb_notime, stderr, LOG_TRACE );
    }
}
