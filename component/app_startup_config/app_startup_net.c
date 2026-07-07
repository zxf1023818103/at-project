#include "app_startup.h"

/* Network-up startup. Deliberately a SEPARATE TU from the console/env path (app_startup.c): the AmebaD
 * QUIC image drops the console libs (project/amebad/app_km4/CMakeLists.txt _diag_grp="") yet still calls
 * app_on_network_up() from its network-up hook. Keeping this self-contained (only the two weak seams,
 * both defined here) means pulling it in does NOT drag app_console_start()'s monitor_start /
 * *_cli_register references into an image that has no console. */

void app_on_network_up(void)
{
    app_network_dns_reinit();          /* c-ares re-point (strong in component/c-ares_config) or no-op */
    app_network_up_start_services();   /* SNTP / AWS / QUIC / iperf (strong per platform in src/main.cpp) */
}

/* Weak no-op defaults (strong-overridden: app_network_up_start_services per platform in src/main.cpp;
 * app_network_dns_reinit by component/c-ares_config when CARES=ON). */
__attribute__((weak)) void app_network_up_start_services(void) { }
__attribute__((weak)) void app_network_dns_reinit(void) { }
