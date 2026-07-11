/*
 * app_startup.h - the shared startup contract between src/main.cpp and the AmebaD port.
 *
 * The shared startup skeleton (app_startup_run + the helpers below) and the Bouffalo / Ameba-newer
 * platform bodies live in src/main.cpp; the AmebaD KM4 body lives in its own TU,
 * the AmebaD section of src/main.cpp too (folded in; XIP-layout-sensitive — boot-test after edits).
 * Both TUs include this header for the hook contract. The startup path shared three near-identical
 * blocks -- LittleFS+env+TZ init, console + CLI registration, and the network-up service start; those
 * live once in src/main.cpp. The weak seams below default to no-ops (in src/main.cpp) and are
 * strong-overridden per platform (AmebaD + Bouffalo in src/main.cpp itself;
 * app_network_dns_reinit by component/c-ares_config when CARES=ON). The symbols the helpers call
 * (vfs_lfs_mount / *_cli_register / monitor_start) resolve at the final image link.
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

/* ---- shared example entry: the whole pre-scheduler startup skeleton --------------------------------
 * Both platform main()s reduce to app_startup_run(). It runs the platform lifecycle hooks below (weak
 * no-ops here; strong-defined per platform from its side) in the canonical order, then starts the
 * scheduler. All platform-specific bring-up lives behind these hooks, so src/main.cpp carries feature
 * gates (AT_PROJECT_*) only and no platform macros (CONFIG_PLATFORM_* / BL*). */
void app_startup_run(void);

/* Console-less variant for the layout-sensitive QUIC self-test image: boot_init + default TZ + WiFi
 * bring-up + scheduler, skipping LittleFS/env/console (that image drops the console libs). Lives in the
 * network TU so it links without dragging the console in. The QUIC handshake self-test starts from the
 * network-up hook. */
void app_startup_run_headless(void);

/* Bring the chip + logger to life: board/clock init, log_lock_init, boot banner, early SDK init
 * (rfparam / flash init on Bouffalo; TF-M NS init + PSA selftests on AmebaD; the platform's
 * pre-scheduler flash write-lock where it does one). Runs first, before any shared step. */
void app_platform_boot_init(void);

/* Seed the wall clock from the retained hardware RTC, after app_env_time_init() loaded the RTC_REF env.
 * Bouffalo: rtc_time_seed(); AmebaD: weak no-op (it seeds inside its WiFi bring-up task). */
void app_platform_rtc_seed(void);

/* Kick off the platform WiFi bring-up: AmebaD spawns its blocking wifi_bringup task; Bouffalo spawns
 * the net-bootstrap task (its event-driven driver does the rest). Runs pre-scheduler. */
void app_platform_wifi_bringup(void);

/* Start any pre-scheduler service tasks not gated on network-up: Bouffalo starts SNTP on the chips that
 * don't post GOT_IP + the NimBLE boot task; AmebaD: weak no-op (its services start on network-up). */
void app_platform_pre_scheduler_services(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STARTUP_H */
