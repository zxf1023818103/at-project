#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "wifi_api.h"
#include "wifi_api_ext.h"
#include "wifi_intf_drv_to_app_internal.h"
#include "whc_ipc_host_nan.h"
#include "lfs_port.h"
#include "nan_network.h"
#include "nan_platform.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef AT_NAN_HEAP_CHECKS
#include "mm.h"
#include "tlsf/tlsf.h"
#endif

int green2_nan_radio_enable(void)
{
#ifdef AT_ENABLE_BLE_NAN_WAKE
    extern void amebagreen2_nan_network_start(void);
    /* The BLE boot task already initialized WLAN for controller calibration. */
    if (!green2_nan_radio_ready()) return -1;
    amebagreen2_nan_network_start();
    return 0;
#else
    extern int wifi_port_enable(void);
    return wifi_port_enable();
#endif
}
int green2_nan_radio_ready(void)
{ extern int amebagreen2_wifi_ready(void); return amebagreen2_wifi_ready(); }
int green2_nan_radio_start(void)
{
    enum { NAN_TX_POWER_DBM = 0 };
    struct rtw_tx_power_ctl_info power = {
        .tx_pwr_force = NAN_TX_POWER_DBM * 4,
        .b_tx_pwr_force_enbale = 1,
    };
    /* Force zero dBm explicitly; the separate enable flag keeps zero from
     * meaning "use the default table". Reapply after every NAN/deep-sleep boot. */
    int rc = wifi_set_tx_power(&power);
    printf("[NAN] fixed TX power=%d dBm rc=%d\n", NAN_TX_POWER_DBM, rc);
    if (rc != RTK_SUCCESS) return rc;
    return whc_host_nan_start(112, RTW_SUPPORT_BAND_2_4G_5G_BOTH);
}
#ifdef AT_GREEN2_NAN_TICKLESS
void green2_nan_tickless_enable(void)
{
    /* Radio/IPC are initialized. Release only the boot veto; other clients'
     * locks remain authoritative, and automatic DSLP still requires teardown. */
    pmu_set_sleep_type(SLEEP_CG);
    pmu_set_sysactive_time(0);
    pmu_release_wakelock(PMU_OS);
    printf("[NAN-PM] AP tickless=%u CG enabled\n", configUSE_TICKLESS_IDLE);
}
#endif
int green2_nan_send(const void *data, size_t len)
{
    struct eth_drv_sg;
    extern int whc_ipc_host_send(int, struct eth_drv_sg *, int, int, void *, uint8_t);
    struct { unsigned int buf, len; } sg = {(unsigned int)data, len};
    return whc_ipc_host_send(2, (struct eth_drv_sg *)&sg, 1, len, NULL, 0);
}

void green2_nan_heap_check(const char *where)
{
    (void)where;
#ifdef AT_NAN_HEAP_CHECKS
    uintptr_t flags = mm_lock_save();
    for (unsigned i = 0; i < CONFIG_MM_HEAP_COUNT; ++i) {
        mm_heap_t *h = g_mem_manager.heaps[i];
        if (h && h->is_active) {
            configASSERT(tlsf_check(h->allocator_priv) == 0);
            configASSERT(tlsf_check_pool(tlsf_get_pool(h->allocator_priv)) == 0);
        }
    }
    __DSB();
    mm_unlock_restore(flags);
#endif
}

int green2_nan_random(void *out, size_t len)
{ return TRNG_get_random_bytes(out, len); }
#ifdef AT_NAN_BLE_OOB
/* The NAN WHC ABI renamed this function; retain the existing controller port's
 * spelling while sending the same NP power-save/coexistence command. */
void wifi_ps_en_by_bt_on(u8 enable) { wifi_ps_en_by_bt_state(enable); }
#endif
int green2_nan_vendor(unsigned short id, const void *data, size_t len)
{
    int rc = whc_host_nan_cfgvendor_cmd(id, data, len);
    green2_nan_heap_check("vendor command returned");
    return rc;
}
int green2_nan_mgmt(const unsigned char *data, size_t len)
{
    struct _raw_data_desc_t d = {0};
    if (!data || len < 30 || len > 512) return -1;
    d.wlan_idx = NAN_WLAN_INDEX; d.buf = (unsigned char *)data; d.buf_len = len;
    return wifi_send_mgnt(&d);
}
/* LittleFS closes/syncs the new record before replacing the old name. */
int green2_nan_store(void *data, size_t len, int write)
{
    lfs_t *fs = vfs_lfs_mount(); lfs_file_t f = {0};
    if (!fs) return -1;
    const char *path = write ? "/nan-pairing.tmp" : "/nan-pairing";
    int rc = lfs_file_open(fs, &f, path, write ? LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC : LFS_O_RDONLY);
    if (rc) return rc;
    lfs_ssize_t n = write ? lfs_file_write(fs, &f, data, len) : lfs_file_read(fs, &f, data, len);
    if (n != (lfs_ssize_t)len || (!write && lfs_file_size(fs, &f) != (lfs_soff_t)len)) rc = -1;
    int close_rc = lfs_file_close(fs, &f);
    if (!rc) rc = close_rc;
    if (!rc && write) rc = lfs_rename(fs, path, "/nan-pairing");
    return rc;
}

int green2_nan_read_mac(void)
{
    struct rtw_mac mac;
    int rc = wifi_get_mac_address(NAN_WLAN_INDEX, &mac, 0);
    if (!rc) green2_nan_mac_set((const uint8_t *)&mac);
    return rc;
}
