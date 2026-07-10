#include "app_startup.h"

#include <stdlib.h>   /* getenv / setenv */
#include <time.h>     /* tzset */

/* Network-up startup. Deliberately a SEPARATE TU from the console/env path (app_startup.c): the AmebaD
 * QUIC image drops the console libs (project/amebad/app_km4/CMakeLists.txt _diag_grp="") yet still calls
 * app_on_network_up() from its network-up hook. Keeping this self-contained (only the two weak seams,
 * both defined here) means pulling it in does NOT drag app_console_start()'s monitor_start /
 * *_cli_register references into an image that has no console. */

/* Console-less startup for the QUIC self-test image (lives here, the console-free TU). Same platform
 * hooks as app_startup_run() minus the LittleFS/env/console path; default TZ only so timestamps read
 * local time. vTaskStartScheduler from FreeRTOS. */
extern void vTaskStartScheduler(void);

void app_startup_run_headless(void)
{
    app_platform_boot_init();
    {
        const char *tz = getenv("TZ");
        if ((tz == NULL) || (tz[0] == '\0'))
            setenv("TZ", "CST-8", 1);
    }
    tzset();
    app_platform_wifi_bringup();
    vTaskStartScheduler();
    for (;;) { }
}

void app_on_network_up(void)
{
    app_network_dns_reinit();          /* c-ares re-point (strong in component/c-ares_config) or no-op */
    app_network_up_start_services();   /* SNTP / AWS / QUIC / iperf (strong per platform in src/main.cpp) */
}

/* Weak no-op defaults (strong-overridden: app_network_up_start_services per platform in src/main.cpp;
 * app_network_dns_reinit by component/c-ares_config when CARES=ON). */
__attribute__((weak)) void app_network_up_start_services(void) { }
__attribute__((weak)) void app_network_dns_reinit(void) { }
