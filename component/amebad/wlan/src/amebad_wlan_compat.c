/*
 * amebad_wlan_compat.c - integrator-provided glue the newer TizenRT
 * lib_wlan_fpu.a / lib_wps_fpu.a expect but the older lib_wlan.a defined
 * internally. Computed as the new-only external symbol delta (nm: symbols
 * undefined in the new wlan+wps pair but defined in the old pair), minus the
 * buckets already satisfied elsewhere:
 *   - rtl_aes / rtl_mpi / rtl_ecp / rtl_md5 ROM crypto -> rom_symbol_crypto.ld
 *   - rom_ssl_ram_map, p_rom_ssl_ram_map              -> mbedtls_rom.ld
 *   - pvPortMalloc, vPortFree, vPortYield, vQueueDelete, xQueueSemaphoreTake -> FreeRTOS
 *   - BKUP_Write (fwlib ROM ld), sys_reset (mbed)      -> already linked
 *   - wifi_config                                      -> alias in usrcfg/rtl8721d_wificfg.c
 * What remains and lives here:
 *   - rtw_printf, rtw_printf_info : the driver log sink (real, -> newlib stdio).
 *   - g_wps_context, wpas_get_wifi_mac : WPS (push-button) state, NOT on our
 *     WPA2-PSK connect path, so link-satisfying backing/stub only.
 *   - rs8721d_set_*_cmd : WoWLAN host-to-firmware command builders, NOT
 *     exercised by connect/iperf, link-satisfying stubs.
 *
 * Built into amebad_wlan_glue (joins the KM4 image2 link group). See
 * doc/amebad_tizenrt_wifi_lib_port.md (Stage 3).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

/* ---- driver log sink ---- */
int rtw_printf(const char *format, ...)
{
	va_list args;
	int n;
	va_start(args, format);
	n = vprintf(format, args);
	va_end(args);
	return n;
}

int rtw_printf_info(const char *format, ...)
{
	va_list args;
	int n;
	va_start(args, format);
	n = vprintf(format, args);
	va_end(args);
	return n;
}

/* ---- WiFi driver config globals ----
 * The older lib_wlan.a exported these tunables; the newer lib externalizes them
 * to the integrator. OUR component/amebad/wifi/wifi_util.c references them (via
 * local `extern u8 ...` decls) and its wext_set_* setters WRITE them from the
 * runtime WiFi config before the driver reads them, so 0 is a safe initial
 * value. Types match the wifi_util.c extern decls (u8 -> unsigned char,
 * uint32_t -> unsigned int, int -> int). */
unsigned char g_tx_ampdu_enable             = 0;
unsigned char rtw_bw_mode                   = 0;
unsigned char rtw_cbw40_enable              = 0;
unsigned char rtw_power_mgnt                = 0;
unsigned char rtw_ant_div_gpio_ext          = 0;
unsigned char rtw_anti_interference_en      = 0;
unsigned char rtw_auto_adaptivity_en        = 0;
unsigned char rtw_set_custom_country_code_en = 0;
unsigned char gk_rekey                      = 0;
unsigned int  gk_rekey_time                 = 0;
unsigned char softap_keep_silent_table_enable = 0;
int           softap_keep_silent_table_interval = 0;

/* ---- WiFi driver glue functions the older lib defined internally ---- */
int rltk_set_pmf(unsigned char mode) { (void)mode; return 0; }
void rtw_wifi_connect_monitor_mgnt(int enable) { (void)enable; }

/* rltk_wlan_{get,set}_link_err: a single link-error bitmask the driver sets on a
 * link fault and wifi_conf.c reads. The older lib kept it internally; provide the
 * trivial storage here. */
static unsigned int s_wlan_link_err = 0;
unsigned int rltk_wlan_get_link_err(void) { return s_wlan_link_err; }
void rltk_wlan_set_link_err(unsigned int errbit) { s_wlan_link_err = errbit; }

/* sys_reset: the driver calls this on a fatal MAC error (rtw_mlme.c) to reboot
 * the chip. The real mbed sys_api.c version (watchdog) is not compiled into this
 * image; a self-contained Cortex-M33 SYSRESETREQ avoids pulling WDG/RTC deps. It
 * is an error-path-only call (never hit on a clean connect/iperf run). */
void sys_reset(void)
{
	volatile unsigned int *AIRCR = (volatile unsigned int *)0xE000ED0CU;
	__asm volatile ("dsb 0xf" ::: "memory");
	*AIRCR = (0x5FAU << 16) | (1U << 2);   /* VECTKEY | SYSRESETREQ */
	__asm volatile ("dsb 0xf" ::: "memory");
	for (;;) { }
}

/* ---- WPS (not on the WPA2-PSK connect path) ----
 * The newer lib_wps externalizes the WPS protocol context (older lib defined it
 * internally). struct rtw_wps_context is about 84 bytes in the sibling rtl8730e
 * driver; a zeroed 256-byte backing store satisfies the link with headroom. WPS
 * is never run here, so it is never touched. */
unsigned char g_wps_context[256] __attribute__((aligned(8)));

/* wpas_get_wifi_mac(dst): WPS helper. WPS-only path, so reporting failure
 * (leaving the caller buffer untouched) is safe; it is never called on PSK. */
int wpas_get_wifi_mac(unsigned char *mac)
{
	(void)mac;
	return -1;
}

/* ---- WoWLAN host-to-firmware command builders (not exercised) ----
 * connect/iperf never arms wake-on-WLAN, so these only satisfy the link. */
int rs8721d_set_global_info_cmd(void)         { return 0; }
int rs8721d_set_disconnect_decision_cmd(void) { return 0; }
int rs8721d_set_keep_alive_cmd(void)          { return 0; }
int rs8721d_set_remote_wake_ctrl_cmd(void)    { return 0; }
int rs8721d_set_wowlan_ctrl_cmd(void)         { return 0; }
