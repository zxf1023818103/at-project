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

/* The usrcfg IPC channel table (rtl8721d_ipccfg.c) wires channel 0 to the
 * monitor's shell_switch_ipc_int (KM0<->KM4 shell forwarding). The QUIC image
 * drops the monitor lib, so provide a weak no-op; the real monitor def overrides
 * this when the shell is linked. The shell IPC channel is unused by the QUIC test. */
__attribute__((weak)) void shell_switch_ipc_int(void *Data, u32 IrqStatus, u32 ChanNum)
{
    (void)Data; (void)IrqStatus; (void)ChanNum;
}
