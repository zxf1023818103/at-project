/*
 * WiFi SCAN-milestone link compat for the KM4 image2.
 *
 * The scan bring-up links lib_wlan.a + the api/wifi sources but intentionally
 * does NOT pull the connect / WPA-Enterprise / promisc / mbed-TLS paths (see
 * the plan: scan only). A few globals/symbols those paths reference are still
 * left undefined at link; this file supplies them:
 *
 *   wifi_mode      — wifi_conf.c only DEFINES this under CONFIG_MBED_ENABLED
 *                    (off in our config); wifi_on() writes it and the scan path
 *                    reads it, so it needs a real definition. Weak so a future
 *                    CONFIG_MBED_ENABLED build's strong def wins.
 *   get_eap_phase/method, eap_autoreconnect_hdl — EAP/802.1X connect path
 *                    (wifi_eap_config.c, not compiled). Returning 0 / no-op is
 *                    the correct "not in EAP" behaviour for a non-EAP scan.
 *   promisc_deinit / promisc_recv_func — promiscuous mode (wifi_promisc.c, not
 *                    compiled); never invoked on the scan path.
 *   mbedtls_platform_set_calloc_free — TLS crypto path (rtw_opt_crypto_ssl.c,
 *                    not compiled).
 *
 * All weak: compiling the real wifi_eap_config.c / wifi_promisc.c / mbedcrypto
 * (when connect lands) overrides every one of these.
 */
#include "wifi_constants.h"   /* rtw_mode_t / RTW_MODE_STA */

__attribute__((weak)) rtw_mode_t wifi_mode = RTW_MODE_STA;

__attribute__((weak)) int  get_eap_phase(void)                 { return 0; }
__attribute__((weak)) int  get_eap_method(void)                { return 0; }
__attribute__((weak)) void eap_autoreconnect_hdl(unsigned char method_id) { (void)method_id; }

__attribute__((weak)) void promisc_deinit(void *padapter)      { (void)padapter; }
__attribute__((weak)) int  promisc_recv_func(void *padapter, void *rframe)
{
    (void)padapter; (void)rframe;
    return 0;
}

__attribute__((weak)) int mbedtls_platform_set_calloc_free(
        void *(*calloc_func)(unsigned int, unsigned int),
        void  (*free_func)(void *))
{
    (void)calloc_func; (void)free_func;
    return 0;
}
