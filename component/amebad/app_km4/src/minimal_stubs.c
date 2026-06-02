/*
 * Weak stubs for the minimal (no-WiFi / no-TLS) KM4 image2.
 *
 * CRYPTO_Init        — the HW crypto engine init (fwlib/crypto). Pulled in by a
 *                      linked object but only exercised on TLS/crypto paths,
 *                      which this minimal firmware doesn't take. No-op success.
 * rtw_if_wifi_thread — referenced by freertos_service.c's task-create wrapper to
 *                      special-case the WiFi driver thread. With no WiFi, report
 *                      "not the wifi thread" (non-zero) so tasks create normally.
 *
 * Weak so the real implementations (HW crypto / the WiFi driver) override these
 * once those subsystems are wired in.
 */
#include "ameba_soc.h"
#include "freertos_service.h"

__attribute__((weak)) int CRYPTO_Init(HAL_CRYPTO_ADAPTER *pIE)
{
    (void)pIE;
    return 0;
}

__attribute__((weak)) int rtw_if_wifi_thread(char *name)
{
    (void)name;
    return 1;
}
