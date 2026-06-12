/*
 * Weak stubs for the AT command-set init hooks (and helpers) that log_service's
 * init_database[] / help handler reference.
 *
 * The minimal "basic AT log" firmware runs the log-service framework (console
 * I/O + the built-in at_log_* commands, which live in log_service.c itself) but
 * does NOT register the larger AT+ command sets. With the SDK's platform_opts.h
 * (CONFIG_WLAN / CONFIG_BT = 1) log_service.c references their init hooks:
 *   at_wifi_init    -> atcmd_wifi.c (the wifi AT+ set + network stack)
 *   at_bt_init      -> atcmd_bt.c   (the bt AT+ set)
 *   print_wlan_help -> atcmd_wifi.c help text
 *   at_sys_init     -> atcmd_sys.c, which calls the mbed gpio/adc/sys HAL
 *                      (a whole mbed-HAL subtree we don't pull for the minimal build)
 *   at_mp_init      -> atcmd_mp.c (manufacturing test)
 *
 * Weak so the real implementations override these when those command sets
 * (and their HAL deps) are wired in later.
 */
__attribute__((weak)) void at_sys_init(void)     { }
__attribute__((weak)) void at_mp_init(void)      { }
__attribute__((weak)) void at_wifi_init(void)    { }
__attribute__((weak)) void at_bt_init(void)      { }
__attribute__((weak)) void print_wlan_help(void) { }
