#ifndef _DIRENT_H_
#define _DIRENT_H_

/*
 * dirent.h — POSIX directory enumeration for the newlib retarget.
 *
 * newlib targets without a native filesystem ship <sys/dirent.h> as a `#error`
 * stub ("<dirent.h> not supported"), so this component provides its own
 * <dirent.h>. component/syscall_config is on the include path AHEAD of the
 * sysroot (target_include_directories ... PUBLIC), so `#include <dirent.h>`
 * resolves here. The matching opendir/readdir/closedir/rewinddir live in
 * newlib_syscalls.c (the LittleFS-backed VFS); libc.a provides none, so the
 * strong defs there win. (mbedTLS x509_crt.c also #include <dirent.h> under
 * MBEDTLS_FS_IO — this header is what lets it compile on this target.)
 *
 * Self-contained on purpose (C standard types only): consumers of <dirent.h>
 * must not be forced to pull in lfs.h / FreeRTOS headers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* d_type values (BSD/glibc convention). */
#define DT_UNKNOWN  0
#define DT_DIR      4
#define DT_REG      8

/* d_name capacity. Matches LittleFS LFS_NAME_MAX (255) + the NUL; kept as a
 * literal so this header stays free of lfs.h. */
#ifndef NAME_MAX
#define NAME_MAX    255
#endif

struct dirent {
    long           d_ino;                  /* inode (unused on LittleFS; 0) */
    unsigned char  d_type;                 /* DT_REG / DT_DIR / DT_UNKNOWN */
    char           d_name[NAME_MAX + 1];   /* NUL-terminated entry name */
};

/* Opaque directory stream (defined in newlib_syscalls.c as struct __dirstream). */
typedef struct __dirstream DIR;

DIR           *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int            closedir(DIR *dirp);
void           rewinddir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* _DIRENT_H_ */
