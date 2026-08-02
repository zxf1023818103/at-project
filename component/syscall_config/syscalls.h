#ifndef AT_SYSCALLS_H
#define AT_SYSCALLS_H

/*
 * syscalls.h — public surface of the cross-platform newlib retarget
 * (component/syscalls/newlib_syscalls.c).
 *
 * Kept dependency-free on purpose — C standard headers only. Consumers that just
 * want the hooks below must NOT be forced to drag in LittleFS / FreeRTOS+TCP
 * headers, so the LittleFS instance is an opaque `struct lfs *` (== lfs_t *) and the
 * socket is an opaque `void *` (the impl casts it back to Socket_t). The TUs that
 * implement these include the real lfs.h / FreeRTOS_Sockets.h themselves.
 */

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform hook (provided by the platform's filesystem port): return the mounted
 * LittleFS instance the fd VFS routes _open'd paths to, or NULL if there is none
 * (in which case _open fails with ENODEV). Referenced only when a consumer actually
 * calls open()/fopen(); --gc-sections drops the file path otherwise. */
struct lfs *vfs_lfs_mount(void);

/* Register an already-created socket as a POSIX fd, so read()/write()/close() on the
 * returned fd dispatch to the platform socket API. `socket` is the native socket
 * handle (FreeRTOS+TCP Socket_t), opaque here. Returns the fd (>=3), or -1/errno. */
int fd_open_socket(void *socket);

/* fd -> the native socket handle registered via fd_open_socket(), or NULL if the fd is
 * not a socket. Used by the BSD-sockets shim (bsd_socket.c) for the bind/connect/
 * setsockopt/select/... operations that don't go through read()/write()/close(). */
void *fd_get_socket(int fd);

/* KV-file-backed environment (env.c). env_init() loads every regular file under KV/
 * into newlib's environ — call once, right after the platform's LittleFS mount.
 * env_save() persists one variable. The consuming image MUST link with
 * -Wl,--wrap=setenv so every setenv() auto-persists KV/<name> (and __real_setenv
 * resolves). */
void env_init(void);
int  env_save(const char *name);

/* RAM Unix-time base (time_base.c) behind newlib's _gettimeofday/time()/clock_gettime
 * (CLOCK_REALTIME). Call from a time source (SNTP setTime / RTC) to set the wall clock;
 * `usec` is the sub-second part (0 if unknown). Before the first call, time() returns
 * 1970 + uptime. The base advances on the FreeRTOS tick (SysTick, 1 ms). */
void amebad_set_unix_time(time_t secs, unsigned int usec);

/* 1 once amebad_set_unix_time() has run (the wall clock is real UTC), else 0
 * (time() is still 1970 + uptime). */
int amebad_time_is_synced(void);

#ifdef __cplusplus
}
#endif

/* Earliest libc bring-up (TLS/thread-pointer on picolibc; no-op on newlib).
 * Call once from main() before any other libc use. */
void at_libc_early_init( void );

#endif /* AT_SYSCALLS_H */
