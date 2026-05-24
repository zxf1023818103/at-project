/* Placeholder TU — bflb_power_mgmt's real sources (pm_manager.c /
 * clock_manager.c / tickless*) are all behind CONFIG_PM_MANAGER /
 * CONFIG_TICKLESS_* flags. CMake refuses to add_library(... STATIC ...)
 * with zero sources, so this gives the lib something to chew on when
 * every gate is off (the BL616 / BL616CL default until wl80211 + wifi6
 * adjacent extraction lands and CONFIG_PM_MANAGER can flip on). */
