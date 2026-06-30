/*
 * smp_img_ota.c - mcumgr img_mgmt + os_mgmt platform impls for AmebaD (RTL8721D).
 *
 * Maps the mcumgr image-upload (img_mgmt_impl_*) onto the existing dual-bank OTA flash sink
 * (ota_flash.c: begin/feed/finish - detects the inactive slot, validates the Realtek 32-byte OTA
 * header + checksum, writes the boot signature last, runs the KM0-XIP spin-park). The uploaded
 * artifact is therefore the same signed OTA image (OTA_All.bin payload) AWS OTA / ota_http use,
 * NOT an MCUboot image - so the MCUboot-specific img_mgmt semantics (hash/version listing, the
 * test/confirm two-phase) are adapted: the slot is armed at upload-finish, write_pending/confirmed
 * are no-ops that succeed, and `image list` (impl_read) is unsupported. Driven over BLE by the
 * smp_bt transport (component/amebad/mcumgr_port). os reset -> deferred sys_reset.
 *
 * Built only for AMEBAD_ENABLE_MCUMGR (implies NIMBLE). See doc / the project_amebad_ota memories.
 */
#include <string.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "mgmt/mgmt.h"
#include "img_mgmt/img_mgmt.h"
#include "img_mgmt/img_mgmt_impl.h"
#include "os_mgmt/os_mgmt.h"
#include "os_mgmt/os_mgmt_impl.h"

#include "ota_flash.h"
#include "at_log.h"

extern void sys_reset(void);   /* sys_api.c (mbed) - WDG full-system reset */

static ota_ctx_t s_ota;
static int       s_ota_active;

/* ------------------------------------------------------------ img_mgmt impl -- */

int
img_mgmt_impl_upload_inspect(const struct img_mgmt_upload_req *req,
                             struct img_mgmt_upload_action *action,
                             const char **errstr)
{
    memset(action, 0, sizeof *action);

    if (req->off == 0) {
        /* First chunk: the image size must be present. We do NOT parse an MCUboot header
         * (the payload is a Realtek OTA image; ota_flash validates its own header). */
        if (req->size == 0 || req->size == (unsigned long long) -1) {
            if (errstr) {
                *errstr = "missing image size";
            }
            return MGMT_ERR_EINVAL;
        }
        action->size = req->size;
        action->area_id = 1;        /* slot 1 = inactive OTA bank */
        action->erase = false;      /* ota_flash erases incrementally during feed */
    } else {
        /* Continuation: the offset must match what we have consumed so far. */
        if (req->off != g_img_mgmt_state.off) {
            action->proceed = false;   /* tells the client to resume at g_img_mgmt_state.off */
            return 0;
        }
        action->size = g_img_mgmt_state.size;
        action->area_id = g_img_mgmt_state.area_id;
    }

    if (req->off + req->data_len > action->size) {
        action->write_bytes = (int) (action->size - req->off);
    } else {
        action->write_bytes = (int) req->data_len;
    }
    action->proceed = true;
    return 0;
}

int
img_mgmt_impl_write_image_data(unsigned int offset, const void *data,
                               unsigned int num_bytes, bool last)
{
    if (offset == 0) {
        if (ota_flash_begin(&s_ota) != 0) {
            return MGMT_ERR_EUNKNOWN;
        }
        s_ota_active = 1;
        log_info("[mcumgr] image upload started");
    }
    if (!s_ota_active) {
        return MGMT_ERR_EBADSTATE;
    }
    if (ota_flash_feed(&s_ota, (const uint8_t *) data, num_bytes) != 0) {
        s_ota_active = 0;
        return MGMT_ERR_EUNKNOWN;
    }
    if (last) {
        /* ota_flash_finish verifies length + the OTA-header checksum over the received bytes (the
         * end-to-end integrity guarantee), flushes the final sector, writes the boot signature last,
         * and arms the slot. Returns <0 on a short/corrupt upload -> we do NOT arm (the client sees
         * an upload error). */
        int rc = ota_flash_finish(&s_ota);
        s_ota_active = 0;
        if (rc != 0) {
            return MGMT_ERR_EUNKNOWN;
        }
        /* Image checksum-verified and slot armed, old slot's flag cleared. Reboot straight into the
         * new image with sys_reset(): it arms a WDG reset with a ~50 ms window, so returning 0 here
         * still lets this final SMP upload response flush over BLE before the reboot fires. */
        log_info("[mcumgr] image verified + armed; rebooting");
        sys_reset();
    }
    return 0;
}

/* ota_flash owns erase + slot arming; these become no-ops / fixed answers. */
int img_mgmt_impl_erase_slot(void)                                   { return 0; }
int img_mgmt_impl_erase_image_data(unsigned int o, unsigned int n)   { (void)o; (void)n; return 0; }
int img_mgmt_impl_erase_if_needed(uint32_t o, uint32_t l)            { (void)o; (void)l; return 0; }
int img_mgmt_impl_write_pending(int slot, bool perm)                 { (void)slot; (void)perm; return 0; }
int img_mgmt_impl_write_confirmed(void)                              { return 0; }
int img_mgmt_impl_swap_type(int slot)                                { (void)slot; return IMG_MGMT_SWAP_TYPE_NONE; }

int
img_mgmt_impl_read(int slot, unsigned int off, void *dst, unsigned int n)
{
    /* The banks hold Realtek OTA images, not MCUboot images, so the header/hash listing that
     * img_mgmt_state_read() would do is meaningless here. `image list` is therefore unsupported;
     * DFU is upload + reset. */
    (void)slot; (void)off; (void)dst; (void)n;
    return MGMT_ERR_ENOTSUP;
}

int
img_mgmt_impl_erased_val(int slot, uint8_t *erased_val)
{
    (void)slot;
    *erased_val = 0xff;   /* NOR flash erases to 0xFF */
    return 0;
}

int img_mgmt_impl_log_upload_start(int s)                    { (void)s; return 0; }
int img_mgmt_impl_log_upload_done(int s, const uint8_t *h)   { (void)s; (void)h; return 0; }
int img_mgmt_impl_log_pending(int s, const uint8_t *h)       { (void)s; (void)h; return 0; }
int img_mgmt_impl_log_confirm(int s, const uint8_t *h)       { (void)s; (void)h; return 0; }

/* ------------------------------------------------------------- os_mgmt impl -- */

static void
smp_reset_task(void *arg)
{
    unsigned int ms = (unsigned int) (uintptr_t) arg;
    vTaskDelay(pdMS_TO_TICKS(ms));     /* let the SMP reset response flush over BLE first */
    log_info("[mcumgr] reset");
    sys_reset();
    for ( ; ; ) {
    }
}

/* Strong override of the weak stub in os_mgmt/stubs.c. */
int
os_mgmt_impl_reset(unsigned int delay_ms)
{
    if (delay_ms < 50) {
        delay_ms = 50;
    }
    if (xTaskCreate(smp_reset_task, "smp_rst", 256, (void *) (uintptr_t) delay_ms,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        return MGMT_ERR_EUNKNOWN;
    }
    return 0;
}

/* ------------------------------------------------------------- registration -- */

void
smp_img_ota_init(void)
{
    img_mgmt_register_group();
    os_mgmt_register_group();
    log_info("[mcumgr] img + os command groups registered");
}
