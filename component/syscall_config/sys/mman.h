/*
 * <sys/mman.h> — iperf3 mmaps a temp-file-backed per-stream buffer. There is no MMU
 * file mapping here; bsd_socket.c backs mmap() with malloc() (the fd/offset are ignored)
 * and munmap() with free(), so the "mapped" buffer is just heap RAM.
 */
#ifndef _AT_SYS_MMAN_H
#define _AT_SYS_MMAN_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED  ((void *)-1)

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* _AT_SYS_MMAN_H */
