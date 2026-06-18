/*
 * env.c — KV-file-backed POSIX environment for the newlib retarget.
 *
 * Persists newlib's `environ` to LittleFS so environment variables survive reboot:
 *   - env_init(): after the FS mounts, load every regular file under KV/ into
 *     environ (filename = variable name, file content = value).
 *   - every setenv() is intercepted (-Wl,--wrap=setenv) and the one changed name is
 *     written back to KV/<name> (or its file removed when unset to "").
 *
 * Standard C/POSIX only (stdio + <dirent.h> + setenv/getenv), so it is portable to
 * any platform that links this newlib retarget. The two LittleFS touch points are
 * reached through libc: fopen/opendir -> _open/opendir -> the VFS in
 * newlib_syscalls.c -> vfs_lfs_mount(); env.c itself pulls in no SoC/FS headers.
 *
 * REQUIRES the consuming image's link to pass `-Wl,--wrap=setenv` (so __real_setenv
 * resolves). A consumer that calls env_init() WITHOUT it fails to link with an
 * undefined __real_setenv — by design (the wrap is the whole point). When nothing
 * calls env_init()/setenv(), --gc-sections drops this TU entirely (zero cost).
 *
 * Modeled on aipi-v1-bw20 component/example/qwen/config.c (its VFS ENV/ loader).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>       /* mkdir() */
#include <dirent.h>         /* opendir/readdir/closedir + DT_REG (this component's) */

#include "syscalls.h"

#define KV_DIR          "KV"
#define ENV_NAME_MAX    64          /* env var name == KV filename */
#define ENV_VALUE_MAX   256         /* env var value (short ASCII; cert bytes live in a FILE) */

/* the real libc setenv, exposed by `-Wl,--wrap=setenv` */
extern int __real_setenv(const char *name, const char *value, int overwrite);

/* Build "KV/<name>" into buf. Returns 0, or -1 if name is empty / too long / overflows. */
static int kv_path(char *buf, size_t buflen, const char *name)
{
    if (name == NULL || name[0] == '\0' || strlen(name) > ENV_NAME_MAX)
        return -1;
    if (sizeof(KV_DIR) + 1 + strlen(name) > buflen)    /* "KV/" + name + NUL */
        return -1;
    strcpy(buf, KV_DIR "/");
    strcat(buf, name);
    return 0;
}

/* Persist one env var to KV/<name> (content = value, no trailing NUL/newline), or
 * remove the backing file when the var is unset. Returns 0 on success, -1 on error. */
int env_save(const char *name)
{
    char        path[ENV_NAME_MAX + sizeof(KV_DIR) + 2];
    const char *value;
    FILE       *f;
    size_t      len, wrote;

    if (kv_path(path, sizeof(path), name) != 0)
        return -1;
    value = getenv(name);
    if (value == NULL) {
        remove(path);                 /* unset -> drop the backing file */
        return 0;
    }
    len = strlen(value);
    if (len > ENV_VALUE_MAX)
        return -1;
    f = fopen(path, "w");
    if (f == NULL)
        return -1;
    wrote = fwrite(value, 1, len, f);
    fclose(f);
    return (wrote == len) ? 0 : -1;
}

/* Load every regular file under KV/ into environ. Call once, right after the
 * platform's LittleFS mount. A missing/empty KV/ is not an error. */
void env_init(void)
{
    DIR           *d;
    struct dirent *e;

    (void)mkdir(KV_DIR, 0);           /* ensure KV/ exists (ignore EEXIST) */
    d = opendir(KV_DIR);
    if (d == NULL)
        return;

    while ((e = readdir(d)) != NULL) {
        char    path[ENV_NAME_MAX + sizeof(KV_DIR) + 2];
        char    value[ENV_VALUE_MAX + 1];
        FILE   *f;
        size_t  n;

        if (e->d_type != DT_REG)
            continue;
        if (kv_path(path, sizeof(path), e->d_name) != 0)
            continue;
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        n = fread(value, 1, sizeof(value) - 1, f);
        fclose(f);
        value[n] = '\0';
        /* trim a trailing CR/LF the host tooling may have appended */
        while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r'))
            value[--n] = '\0';
        /* __real_setenv: loading must not re-trigger the --wrap save hook */
        (void)__real_setenv(e->d_name, value, 1);
    }
    closedir(d);
}

/* setenv() interceptor (-Wl,--wrap=setenv): set via the real libc, then persist the
 * single changed name to KV/. So a runtime `setenv("endpoint", ...)` is durable
 * across reboot with no explicit save call. */
int __wrap_setenv(const char *name, const char *value, int overwrite)
{
    int r = __real_setenv(name, value, overwrite);
    if (r == 0)
        env_save(name);
    return r;
}
