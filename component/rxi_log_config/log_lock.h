#ifndef LOG_LOCK_H
#define LOG_LOCK_H

/*
 * Thread-safety init for rxi/log.c. Creates a FreeRTOS mutex and installs it via log_set_lock()
 * so concurrent tasks don't interleave a log line. Call once at startup (task context). The
 * logger's default output (fprintf -> stderr) is unchanged. See log_lock.c.
 */
#ifdef __cplusplus
extern "C" {
#endif

void log_lock_init( void );

#ifdef __cplusplus
}
#endif

#endif /* LOG_LOCK_H */
