#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "wifi_api.h"
#include "wifi_intf_drv_to_app_internal.h"
#include "whc_ipc_host_nan.h"
#include "lfs_port.h"
#include "nan_network.h"
#include <stddef.h>
#include <stdint.h>
#ifdef AT_NAN_HEAP_CHECKS
#include "mm.h"
#include "tlsf/tlsf.h"
#endif

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
