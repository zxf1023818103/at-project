/*
 * ble_store_lfs.c - a LittleFS-persistent BLE store for the AmebaD NimBLE host.
 *
 * NimBLE keeps bonding security material (our/peer LTK+IRK+CSRK) and CCCD flags
 * in a "store" reached via ble_hs_cfg.store_{read,write,delete}_cb. The bundled
 * host/store/ram backend keeps them in RAM only, so they vanish on reboot; the
 * host/store/config backend persists but needs Mynewt's sysinit `config`
 * subsystem we don't build.
 *
 * This is a drop-in store that mirrors the RAM backend's lookup/match logic but
 * serializes the whole table to a single LittleFS file (BLE_STORE_LFS_PATH) on
 * every successful write/delete, and reloads it at init. Records are POD
 * (ble_store_value_sec / _cccd hold no pointers), so a flat fwrite of the arrays
 * round-trips. File I/O rides newlib stdio over the LFS VFS (same path as
 * aws_provision.c's cert file).
 *
 * Call ble_store_lfs_init() from amebad_nimble_start() (after nimble_port_init)
 * instead of ble_store_ram_init().
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "syscfg/syscfg.h"
#include "host/ble_hs.h"

#define BLE_STORE_LFS_PATH    "/ble_store.bin"
#define BLE_STORE_LFS_MAGIC   0x424c4653u   /* "BLFS" */
#define BLE_STORE_LFS_VERSION 1u

#define MAX_BONDS  MYNEWT_VAL(BLE_STORE_MAX_BONDS)
#define MAX_CCCDS  MYNEWT_VAL(BLE_STORE_MAX_CCCDS)

static struct ble_store_value_sec  s_our_secs[MAX_BONDS];
static struct ble_store_value_sec  s_peer_secs[MAX_BONDS];
static struct ble_store_value_cccd s_cccds[MAX_CCCDS];
static int s_num_our_secs;
static int s_num_peer_secs;
static int s_num_cccds;

/* On-disk header. The record sizes + caps are stamped so a config change (or a
 * struct-layout change) invalidates a stale file instead of mis-parsing it. */
struct ble_store_lfs_hdr {
    uint32_t magic;
    uint32_t version;
    uint16_t sz_sec;
    uint16_t sz_cccd;
    uint16_t max_bonds;
    uint16_t max_cccds;
    int32_t  num_our;
    int32_t  num_peer;
    int32_t  num_cccd;
};

/*****************************************************************************
 * persistence
 *****************************************************************************/

static void
ble_store_lfs_save(void)
{
    struct ble_store_lfs_hdr hdr;
    FILE *f;

    f = fopen(BLE_STORE_LFS_PATH, "w");
    if (f == NULL) {
        printf("[BLEstore] save: cannot open %s\n\r", BLE_STORE_LFS_PATH);
        return;
    }

    memset(&hdr, 0, sizeof hdr);
    hdr.magic     = BLE_STORE_LFS_MAGIC;
    hdr.version   = BLE_STORE_LFS_VERSION;
    hdr.sz_sec    = (uint16_t)sizeof(struct ble_store_value_sec);
    hdr.sz_cccd   = (uint16_t)sizeof(struct ble_store_value_cccd);
    hdr.max_bonds = (uint16_t)MAX_BONDS;
    hdr.max_cccds = (uint16_t)MAX_CCCDS;
    hdr.num_our   = s_num_our_secs;
    hdr.num_peer  = s_num_peer_secs;
    hdr.num_cccd  = s_num_cccds;

    fwrite(&hdr, 1, sizeof hdr, f);
    if (s_num_our_secs > 0) {
        fwrite(s_our_secs, sizeof s_our_secs[0], s_num_our_secs, f);
    }
    if (s_num_peer_secs > 0) {
        fwrite(s_peer_secs, sizeof s_peer_secs[0], s_num_peer_secs, f);
    }
    if (s_num_cccds > 0) {
        fwrite(s_cccds, sizeof s_cccds[0], s_num_cccds, f);
    }
    fclose(f);
}

static void
ble_store_lfs_load(void)
{
    struct ble_store_lfs_hdr hdr;
    FILE *f;

    s_num_our_secs = 0;
    s_num_peer_secs = 0;
    s_num_cccds = 0;

    f = fopen(BLE_STORE_LFS_PATH, "r");
    if (f == NULL) {
        return;                 /* no bonds yet — fresh device */
    }

    if (fread(&hdr, 1, sizeof hdr, f) != sizeof hdr ||
        hdr.magic != BLE_STORE_LFS_MAGIC ||
        hdr.version != BLE_STORE_LFS_VERSION ||
        hdr.sz_sec != sizeof(struct ble_store_value_sec) ||
        hdr.sz_cccd != sizeof(struct ble_store_value_cccd) ||
        hdr.max_bonds != MAX_BONDS ||
        hdr.max_cccds != MAX_CCCDS ||
        hdr.num_our  < 0 || hdr.num_our  > MAX_BONDS ||
        hdr.num_peer < 0 || hdr.num_peer > MAX_BONDS ||
        hdr.num_cccd < 0 || hdr.num_cccd > MAX_CCCDS) {
        printf("[BLEstore] load: stale/invalid %s — ignoring\n\r", BLE_STORE_LFS_PATH);
        fclose(f);
        return;
    }

    if (hdr.num_our > 0 &&
        (int)fread(s_our_secs, sizeof s_our_secs[0], hdr.num_our, f) == hdr.num_our) {
        s_num_our_secs = hdr.num_our;
    }
    if (hdr.num_peer > 0 &&
        (int)fread(s_peer_secs, sizeof s_peer_secs[0], hdr.num_peer, f) == hdr.num_peer) {
        s_num_peer_secs = hdr.num_peer;
    }
    if (hdr.num_cccd > 0 &&
        (int)fread(s_cccds, sizeof s_cccds[0], hdr.num_cccd, f) == hdr.num_cccd) {
        s_num_cccds = hdr.num_cccd;
    }
    fclose(f);

    printf("[BLEstore] loaded %d bond(s) (our=%d peer=%d) + %d cccd(s) from %s\n\r",
           s_num_peer_secs, s_num_our_secs, s_num_peer_secs, s_num_cccds,
           BLE_STORE_LFS_PATH);
}

/*****************************************************************************
 * sec lookup/match (mirrors host/store/ram)
 *****************************************************************************/

static int
find_sec(const struct ble_store_key_sec *key_sec,
         const struct ble_store_value_sec *secs, int num)
{
    int i;

    if (!ble_addr_cmp(&key_sec->peer_addr, BLE_ADDR_ANY)) {
        if (key_sec->idx < num) {
            return key_sec->idx;
        }
    } else if (key_sec->idx == 0) {
        for (i = 0; i < num; i++) {
            if (!ble_addr_cmp(&secs[i].peer_addr, &key_sec->peer_addr)) {
                return i;
            }
        }
    }
    return -1;
}

static int
read_sec(const struct ble_store_key_sec *key_sec,
         const struct ble_store_value_sec *secs, int num,
         struct ble_store_value_sec *out)
{
    int idx = find_sec(key_sec, secs, num);
    if (idx == -1) {
        return BLE_HS_ENOENT;
    }
    *out = secs[idx];
    return 0;
}

static int
write_sec(struct ble_store_value_sec *secs, int *num,
          const struct ble_store_value_sec *val)
{
    struct ble_store_key_sec key_sec;
    int idx;

    ble_store_key_from_value_sec(&key_sec, val);
    idx = find_sec(&key_sec, secs, *num);
    if (idx == -1) {
        if (*num >= MAX_BONDS) {
            return BLE_HS_ESTORE_CAP;
        }
        idx = (*num)++;
    }
    secs[idx] = *val;
    return 0;
}

static int
delete_obj(void *values, int value_size, int idx, int *num)
{
    uint8_t *dst;

    (*num)--;
    if (idx < *num) {
        dst = (uint8_t *)values + idx * value_size;
        memmove(dst, dst + value_size, (size_t)(*num - idx) * value_size);
    }
    return 0;
}

static int
delete_sec(const struct ble_store_key_sec *key_sec,
           struct ble_store_value_sec *secs, int *num)
{
    int idx = find_sec(key_sec, secs, *num);
    if (idx == -1) {
        return BLE_HS_ENOENT;
    }
    return delete_obj(secs, sizeof secs[0], idx, num);
}

/*****************************************************************************
 * cccd lookup/match (mirrors host/store/ram)
 *****************************************************************************/

static int
find_cccd(const struct ble_store_key_cccd *key)
{
    int skipped = 0;
    int i;

    for (i = 0; i < s_num_cccds; i++) {
        if (ble_addr_cmp(&key->peer_addr, BLE_ADDR_ANY)) {
            if (ble_addr_cmp(&s_cccds[i].peer_addr, &key->peer_addr)) {
                continue;
            }
        }
        if (key->chr_val_handle != 0) {
            if (s_cccds[i].chr_val_handle != key->chr_val_handle) {
                continue;
            }
        }
        if (key->idx > skipped) {
            skipped++;
            continue;
        }
        return i;
    }
    return -1;
}

/*****************************************************************************
 * store API callbacks
 *****************************************************************************/

static int
ble_store_lfs_read(int obj_type, const union ble_store_key *key,
                   union ble_store_value *value)
{
    switch (obj_type) {
    case BLE_STORE_OBJ_TYPE_PEER_SEC:
        return read_sec(&key->sec, s_peer_secs, s_num_peer_secs, &value->sec);
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
        return read_sec(&key->sec, s_our_secs, s_num_our_secs, &value->sec);
    case BLE_STORE_OBJ_TYPE_CCCD: {
        int idx = find_cccd(&key->cccd);
        if (idx == -1) {
            return BLE_HS_ENOENT;
        }
        value->cccd = s_cccds[idx];
        return 0;
    }
    default:
        return BLE_HS_ENOTSUP;
    }
}

static int
ble_store_lfs_write(int obj_type, const union ble_store_value *val)
{
    int rc;

    switch (obj_type) {
    case BLE_STORE_OBJ_TYPE_PEER_SEC:
        rc = write_sec(s_peer_secs, &s_num_peer_secs, &val->sec);
        break;
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
        rc = write_sec(s_our_secs, &s_num_our_secs, &val->sec);
        break;
    case BLE_STORE_OBJ_TYPE_CCCD: {
        struct ble_store_key_cccd key_cccd;
        int idx;
        ble_store_key_from_value_cccd(&key_cccd, &val->cccd);
        idx = find_cccd(&key_cccd);
        if (idx == -1) {
            if (s_num_cccds >= MAX_CCCDS) {
                return BLE_HS_ESTORE_CAP;
            }
            idx = s_num_cccds++;
        }
        s_cccds[idx] = val->cccd;
        rc = 0;
        break;
    }
    default:
        return BLE_HS_ENOTSUP;
    }

    if (rc == 0) {
        ble_store_lfs_save();
    }
    return rc;
}

static int
ble_store_lfs_delete(int obj_type, const union ble_store_key *key)
{
    int rc;

    switch (obj_type) {
    case BLE_STORE_OBJ_TYPE_PEER_SEC:
        rc = delete_sec(&key->sec, s_peer_secs, &s_num_peer_secs);
        break;
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
        rc = delete_sec(&key->sec, s_our_secs, &s_num_our_secs);
        break;
    case BLE_STORE_OBJ_TYPE_CCCD: {
        int idx = find_cccd(&key->cccd);
        if (idx == -1) {
            return BLE_HS_ENOENT;
        }
        rc = delete_obj(s_cccds, sizeof s_cccds[0], idx, &s_num_cccds);
        break;
    }
    default:
        return BLE_HS_ENOTSUP;
    }

    if (rc == 0) {
        ble_store_lfs_save();
    }
    return rc;
}

void
ble_store_lfs_init(void)
{
    ble_store_lfs_load();
    ble_hs_cfg.store_read_cb   = ble_store_lfs_read;
    ble_hs_cfg.store_write_cb  = ble_store_lfs_write;
    ble_hs_cfg.store_delete_cb = ble_store_lfs_delete;
}
