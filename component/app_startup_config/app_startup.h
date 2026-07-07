/*
 * component/app_startup_config - shared startup helpers for both platform main()s.
 *
 * src/main.cpp holds two platform main()s (AmebaD KM4, Bouffalo BL602/BL616/...). They shared three
 * near-identical startup blocks: LittleFS+env+TZ init, console + CLI registration, and the network-up
 * service start. Those blocks live here once; each main() calls the helpers and supplies the
 * platform-specific parts through the weak seams below. The seams default to no-ops here and are
 * strong-overridden per platform in src/main.cpp (app_network_dns_reinit is strong-overridden by
 * component/c-ares_config when CARES=ON). The symbols the helpers call (vfs_lfs_mount / *_cli_register /
 * monitor_start) resolve at the final image link, same forward-extern style src/main.cpp already uses.
 */
#ifndef APP_STARTUP_H
#define APP_STARTUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Mount LittleFS (vfs_lfs_mount) + load persisted POSIX env (env_init) + default TZ to CST-8 (kept if
 * already set) + tzset(). Call once from main(), pre-scheduler. */
void app_env_time_init(void);

/* Start the shared interactive console: monitor_start() + the fs/env/time CLI commands, then
 * app_console_register_platform() for the platform + feature-gated commands. */
void app_console_start(void);

/* Call from each platform's network-up handler once the link is genuinely up: (re)point the DNS
 * resolver (app_network_dns_reinit) then start the app services (app_network_up_start_services). */
void app_on_network_up(void);

/* ---- platform seams: weak no-op defaults in app_startup.c; strong-overridden per platform -------- */

/* Register the platform + feature-gated console commands (reboot/flash/wifi/aws/...). Defined strong
 * in src/main.cpp per platform, under that platform's feature #ifdefs. */
void app_console_register_platform(void);

/* Start the network services on link-up (SNTP / AWS / QUIC / iperf). Defined strong in src/main.cpp
 * per platform. */
void app_network_up_start_services(void);

/* (Re)point the DNS resolver at the current servers. No-op by default; component/c-ares_config supplies
 * a strong override (cares_resolver_init + cares_resolver_set_servers) when linked (CARES=ON). */
void app_network_dns_reinit(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STARTUP_H */
