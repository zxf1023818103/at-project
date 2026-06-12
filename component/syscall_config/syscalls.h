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

#ifdef __cplusplus
}
#endif

#endif /* AT_SYSCALLS_H */
