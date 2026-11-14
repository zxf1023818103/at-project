/* SPDX-License-Identifier: Apache-2.0
 * Reuse mcumgr's upload/state handlers; adapt native Telink image information
 * and physical flash I/O, without presenting the binary as an MCUboot image.
 */
#include "ota_storage.h"
#include "mcumgr_compat.h"
#include "img_mgmt/img_mgmt.h"
#include "img_mgmt/img_mgmt_impl.h"
#include "img_mgmt/image.h"
#include "mgmt/mgmt.h"
#include <string.h>

static int status(int rc)
{
    switch (rc) {
    case TL_OTA_OK: return 0;
    case TL_OTA_INVALID: return MGMT_ERR_EINVAL;
    case TL_OTA_STATE: return MGMT_ERR_EBADSTATE;
    case TL_OTA_HASH: return MGMT_ERR_ECORRUPT;
    case TL_OTA_NOT_FOUND: return MGMT_ERR_ENOENT;
    default: return MGMT_ERR_EUNKNOWN;
    }
}
int img_mgmt_read_info(int slot, struct image_version *version, uint8_t *hash, uint32_t *flags)
{
    TlOtaImage info;
    int rc=tl_ota_info((unsigned)slot, &info);
    if (rc) return status(rc);
    if (version) {
        memset(version, 0, sizeof *version);
        version->iv_minor=1; version->iv_build_num=info.build;
    }
    if (hash) memcpy(hash, info.hash, 32);
    if (flags) *flags=0;
    return 0;
}
int img_mgmt_impl_upload_inspect(const struct img_mgmt_upload_req *req,
                               struct img_mgmt_upload_action *action, const char **errstr)
{
    (void)errstr;
    memset(action, 0, sizeof *action);
    if (!tl_ota.ready || tl_ota.pending) return MGMT_ERR_EBADSTATE;
    if (req->image || req->off>TL_OTA_IMAGE_MAX || !req->data_len) return MGMT_ERR_EINVAL;
    if (!req->off) {
        uint32_t build;
        if (req->size>TL_OTA_IMAGE_MAX || req->data_sha_len!=32 ||
            tl_ota_header(req->img_data, req->data_len, (uint32_t)req->size, &build) ||
            (req->upgrade && build<=tl_ota.running.build)) return MGMT_ERR_EINVAL;
        if (tl_ota.uploading && tl_ota.size==req->size && !memcmp(req->data_sha, tl_ota.expected, 32))
            return 0; /* Same session: return the existing offset, even after reconnect. */
        action->size=req->size;
    } else {
        if (!tl_ota.uploading || req->off!=g_img_mgmt_state.off) return 0;
        action->size=g_img_mgmt_state.size;
    }
    if (req->off>action->size || req->data_len>action->size-req->off) return MGMT_ERR_EINVAL;
    action->area_id=1; action->write_bytes=req->data_len; action->proceed=true;
    return 0;
}
int img_mgmt_impl_write_image_data(unsigned int offset, const void *data, unsigned int length, bool last)
{
    if (!offset) {
        int rc=tl_ota_begin(g_img_mgmt_state.size, g_img_mgmt_state.data_sha, data, length);
        if (rc) return status(rc);
    }
    return status(tl_ota_write(offset, data, length, last));
}
int img_mgmt_impl_write_pending(int slot, bool permanent)
{
    if (slot!=1) return MGMT_ERR_EINVAL;
    if (!permanent) return MGMT_ERR_ENOTSUP;
    return status(tl_ota_activate());
}
int img_mgmt_impl_write_confirmed(void) { return tl_ota.pending ? MGMT_ERR_EBADSTATE : 0; }
int img_mgmt_impl_swap_type(int slot)
{ (void)slot; return tl_ota.pending ? IMG_MGMT_SWAP_TYPE_PERM : IMG_MGMT_SWAP_TYPE_NONE; }
int img_mgmt_impl_erase_slot(void)
{
    int rc=tl_ota_erase();
    if (!rc) memset(&g_img_mgmt_state, 0, sizeof g_img_mgmt_state);
    return status(rc);
}
int img_mgmt_impl_read(int slot, unsigned int off, void *data, unsigned int length)
{ (void)slot; (void)off; (void)data; (void)length; return MGMT_ERR_ENOTSUP; }
int img_mgmt_impl_erased_val(int slot, uint8_t *value) { (void)slot; *value=0xff; return 0; }
int img_mgmt_impl_erase_image_data(unsigned int off, unsigned int len)
{ (void)off; (void)len; return MGMT_ERR_ENOTSUP; }
int img_mgmt_impl_erase_if_needed(uint32_t off, uint32_t len)
{ (void)off; (void)len; return MGMT_ERR_ENOTSUP; }
int img_mgmt_impl_log_upload_start(int rc) { return rc; }
int img_mgmt_impl_log_upload_done(int rc, const uint8_t *hash) { (void)hash; return rc; }
int img_mgmt_impl_log_pending(int rc, const uint8_t *hash) { (void)hash; return rc; }
int img_mgmt_impl_log_confirm(int rc, const uint8_t *hash) { (void)hash; return rc; }
