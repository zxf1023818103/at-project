#include "app_startup.h"

#include <stdio.h>    /* printf */
#include <stdlib.h>   /* getenv, setenv */
#include <time.h>     /* tzset */

/* Console + env/time startup. Kept in its own TU (app_on_network_up lives in app_startup_net.c) so an
 * image that drops the console libs (the AmebaD QUIC image: _diag_grp="") but still calls
 * app_on_network_up() does NOT drag this TU's monitor_start / *_cli_register references in.
 *
 * Symbols we call resolve at the final image link (same forward-extern style src/main.cpp uses), so
 * this glue TU stays free of every subsystem's headers. vfs_lfs_mount() is the shared mount name on
 * both platforms (AmebaD defines it as a thin wrapper over amebad_lfs_mount). */
extern void *vfs_lfs_mount(void);
extern void  env_init(void);
extern void  monitor_start(void);
extern void  fs_cli_register(void);
extern void  env_cli_register(void);
extern void  time_cli_register(void);

void app_env_time_init(void)
{
    void *lfs = vfs_lfs_mount();
    printf("[LFS] mount %s\n", lfs ? "OK" : "FAILED");
    if (lfs != NULL)
        env_init();   /* load KV/ into newlib environ (the --wrap=setenv hook then persists runtime setenv) */

    /* Default the wall clock + log timestamps to China Standard Time (UTC+8). Only when TZ is
     * unset/empty (a persisted/user TZ is kept); tzset() loads it into newlib's _timezone (read by
     * the console timestamp). This also seeds the first env var so `env` isn't empty on a fresh box. */
    {
        const char *tz = getenv("TZ");
        if ((tz == NULL) || (tz[0] == '\0'))
            setenv("TZ", "CST-8", 1);
    }
    tzset();
}

void app_console_start(void)
{
    monitor_start();      /* shared engine: line editor + generic version/heap/tasks/echo */
    fs_cli_register();    /* ls/cat/rm/rmdir/mkdir/mv/cp/touch/hexdump/df/mkfs/chmod + hashes */
    env_cli_register();   /* setenv/getenv/env */
    time_cli_register();  /* date/uptime/sntp_update */
    app_console_register_platform();
}

/* Weak no-op default (strong-overridden per platform in src/main.cpp). Lives in this (console) TU so
 * it is only pulled into images that link the console. */
__attribute__((weak)) void app_console_register_platform(void) { }
