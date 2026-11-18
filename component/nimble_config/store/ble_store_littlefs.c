/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_store_littlefs.h"
#include "host/ble_hs_hci.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static BleStoreLittlefs *store;
enum { HEADER=BLE_BOND_HEADER, SEC=BLE_BOND_SEC, CCCD=BLE_BOND_CCCD,
       OUR=8, PEER=9, SUBS=10 };
#define get32 ble_bond_get32
static int fail(int rc)
{ ble_bond_fail(store,rc); return BLE_HS_ESTORE_FAIL; }
static int save(void)
{ return ble_bond_save(store) ? BLE_HS_ESTORE_FAIL : 0; }
static uint8_t *records(int type, unsigned *size, unsigned *capacity, unsigned *count)
{ return ble_bond_records(store,type,size,capacity,count); }
static int match(int type, const union ble_store_key *key, const uint8_t *table,
                  unsigned size, unsigned count)
{
    const ble_addr_t *address = type == BLE_STORE_OBJ_TYPE_CCCD ? &key->cccd.peer_addr : &key->sec.peer_addr;
    unsigned skip = type == BLE_STORE_OBJ_TYPE_CCCD ? key->cccd.idx : key->sec.idx;
    for (unsigned i=0; i<count; ++i) {
        const uint8_t *p = table + i*size;
        if (ble_addr_cmp(address, BLE_ADDR_ANY) &&
            (p[0] != address->type || memcmp(p+1, address->val, 6))) continue;
        if (type == BLE_STORE_OBJ_TYPE_CCCD && get32(p+12)!=BLE_STORE_GATT_DATABASE_ID) continue;
        if (type == BLE_STORE_OBJ_TYPE_CCCD && key->cccd.chr_val_handle &&
            key->cccd.chr_val_handle != (unsigned)(p[7] | p[8]<<8)) continue;
        if (skip) { --skip; continue; }
        return (int)i;
    }
    return -1;
}
static void encode(int type, uint8_t *p, const union ble_store_value *v)
{
    const ble_addr_t *a = type == BLE_STORE_OBJ_TYPE_CCCD ? &v->cccd.peer_addr : &v->sec.peer_addr;
    p[0] = a->type; memcpy(p+1, a->val, 6);
    if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        p[7]=v->cccd.chr_val_handle; p[8]=v->cccd.chr_val_handle>>8;
        p[9]=v->cccd.flags; p[10]=v->cccd.flags>>8; p[11]=!!v->cccd.value_changed;
        ble_bond_put32(p+12,BLE_STORE_GATT_DATABASE_ID);
    } else {
        p[7]=v->sec.key_size; p[8]=v->sec.ediv; p[9]=v->sec.ediv>>8;
        for (unsigned i=0; i<8; ++i) p[10+i]=(uint8_t)(v->sec.rand_num>>(8*i));
        memcpy(p+18,v->sec.ltk,16); memcpy(p+34,v->sec.irk,16); memcpy(p+50,v->sec.csrk,16);
        p[66]=v->sec.ltk_present | v->sec.irk_present<<1 | v->sec.csrk_present<<2 |
              v->sec.authenticated<<3 | v->sec.sc<<4;
        p[73]|=1;
    }
}
static void decode(int type, const uint8_t *p, union ble_store_value *v)
{
    /* ble_store_read_* passes a pointer to the individual member, which can
     * be smaller than the union (notably CCCD). */
    if (type == BLE_STORE_OBJ_TYPE_CCCD) memset(&v->cccd, 0, sizeof v->cccd);
    else memset(&v->sec, 0, sizeof v->sec);
    ble_addr_t *a = type == BLE_STORE_OBJ_TYPE_CCCD ? &v->cccd.peer_addr : &v->sec.peer_addr;
    a->type=p[0]; memcpy(a->val,p+1,6);
    if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        v->cccd.chr_val_handle=p[7]|p[8]<<8; v->cccd.flags=p[9]|p[10]<<8;
        v->cccd.value_changed=!!p[11];
    } else {
        v->sec.key_size=p[7]; v->sec.ediv=p[8]|p[9]<<8;
        for (unsigned i=0; i<8; ++i) v->sec.rand_num |= (uint64_t)p[10+i]<<(8*i);
        memcpy(v->sec.ltk,p+18,16); memcpy(v->sec.irk,p+34,16); memcpy(v->sec.csrk,p+50,16);
        v->sec.ltk_present=!!(p[66]&1); v->sec.irk_present=!!(p[66]&2);
        v->sec.csrk_present=!!(p[66]&4); v->sec.authenticated=!!(p[66]&8); v->sec.sc=!!(p[66]&16);
    }
}
static int read_cb(int type, const union ble_store_key *key, union ble_store_value *v)
{
    if (!store || !store->ready) return BLE_HS_ESTORE_FAIL;
    unsigned size, capacity, count;
    uint8_t *table=records(type,&size,&capacity,&count);
    if (!table) return BLE_HS_ENOTSUP;
    int i=match(type,key,table,size,store->blob[count]);
    if (i<0) return BLE_HS_ENOENT;
    const uint8_t *p=table+(unsigned)i*size;
    /* Old Bluedroid legacy LENC blobs stored DIV but discarded EDIV/Rand.
     * Those cannot answer a NimBLE legacy LTK lookup. Never invent zeroes;
     * newly paired bonds capture this metadata at key distribution time. */
    if (type==BLE_STORE_OBJ_TYPE_OUR_SEC && (p[66]&BLE_BOND_LTK) &&
        !(p[66]&BLE_BOND_SC) && !(p[73]&1)) return BLE_HS_ENOTSUP;
    decode(type,p,v);
    return 0;
}
static int write_cb(int type, const union ble_store_value *v)
{
    if (!store || !store->ready) return BLE_HS_ESTORE_FAIL;
    unsigned size, capacity, count;
    uint8_t *table=records(type,&size,&capacity,&count);
    if (!table) return BLE_HS_ENOTSUP;
    union ble_store_key key={0};
    if (type == BLE_STORE_OBJ_TYPE_CCCD) ble_store_key_from_value_cccd(&key.cccd,&v->cccd);
    else ble_store_key_from_value_sec(&key.sec,&v->sec);
    if (key.sec.peer_addr.type > BLE_ADDR_RANDOM) return BLE_HS_EINVAL;
    if (type == BLE_STORE_OBJ_TYPE_CCCD) {
        if (!v->cccd.chr_val_handle || (v->cccd.flags & ~3u)) return BLE_HS_EINVAL;
    } else if (!(v->sec.ltk_present || v->sec.irk_present || v->sec.csrk_present) ||
               (v->sec.ltk_present && (v->sec.key_size < 7 || v->sec.key_size > 16))) return BLE_HS_EINVAL;
    int i=match(type,&key,table,size,store->blob[count]);
    if (i<0) {
        if (store->blob[count] >= capacity) return BLE_HS_ESTORE_CAP;
        i=store->blob[count]++;
    }
    uint8_t encoded[SEC]={0};
    /* Preserve Host-neutral metadata NimBLE does not expose (DIV/counters).
     * A newly negotiated CSRK resets its counter; a repeat write does not. */
    if (type!=BLE_STORE_OBJ_TYPE_CCCD) memcpy(encoded,table+(unsigned)i*size,size);
    encode(type,encoded,v);
    if (type!=BLE_STORE_OBJ_TYPE_CCCD && memcmp(encoded+50,table+(unsigned)i*size+50,16))
        memset(encoded+67,0,4);
    if (!memcmp(table+(unsigned)i*size,encoded,size)) return 0;
    memcpy(table+(unsigned)i*size,encoded,size);
    return save();
}
static int delete_cb(int type, const union ble_store_key *key)
{
    if (!store || !store->ready) return BLE_HS_ESTORE_FAIL;
    unsigned size, capacity, count;
    uint8_t *table=records(type,&size,&capacity,&count);
    if (!table) return BLE_HS_ENOTSUP;
    int i=match(type,key,table,size,store->blob[count]);
    if (i<0) return BLE_HS_ENOENT;
    --store->blob[count];
    memmove(table+(unsigned)i*size,table+((unsigned)i+1)*size,(store->blob[count]-(unsigned)i)*size);
    memset(table+store->blob[count]*size,0,size);
    return save();
}
static int gen_key(uint8_t type, struct ble_store_gen_key *key, uint16_t connection)
{
    (void)connection;
    if (type != BLE_STORE_GEN_KEY_IRK) return BLE_HS_ENOTSUP;
    if (!store || !store->ready) return BLE_HS_ESTORE_FAIL;
    if (!(store->blob[7]&1)) {
        int rc=ble_hs_hci_rand(store->blob+12,16);
        if (rc) return fail(rc);
        store->blob[7]|=1;
        rc=save(); if (rc) return rc;
    }
    memcpy(key->irk,store->blob+12,16);
    return 0;
}
/* Import the former raw-struct file only if its ABI and complete length
 * match. Leave the original untouched. The next IRK generation commits BLS2;
 * the old backend did not persist a local IRK. No partial bond table is accepted. */
static int import_legacy(lfs_t *fs)
{
    uint8_t cache[LFS_BLOB_CACHE_MAX], header[28];
    struct lfs_file_config cfg={.buffer=cache};
    lfs_file_t file;
    if (fs->cfg->cache_size>sizeof cache) return LFS_ERR_INVAL;
    int rc=lfs_file_opencfg(fs,&file,"/ble_store.bin",LFS_O_RDONLY,&cfg);
    if (rc==LFS_ERR_NOENT) return 0;
    if (rc) return rc;
    lfs_ssize_t n=lfs_file_read(fs,&file,header,sizeof header);
    unsigned our=0, peer=0, cccd=0;
    if (n<0) rc=n;
    else if (n!=sizeof header) rc=LFS_ERR_CORRUPT;
    else {
        our=get32(header+16); peer=get32(header+20); cccd=get32(header+24);
        unsigned sec_size=header[8]|header[9]<<8, cccd_size=header[10]|header[11]<<8;
        unsigned bonds=header[12]|header[13]<<8, cccds=header[14]|header[15]<<8;
        if (get32(header)!=0x424c4653u || get32(header+4)!=1 ||
            sec_size!=sizeof(struct ble_store_value_sec) ||
            cccd_size!=sizeof(struct ble_store_value_cccd) ||
            our>BLE_LFS_BONDS || peer>BLE_LFS_BONDS || cccd>BLE_LFS_CCCDS ||
            our>bonds || peer>bonds || cccd>cccds ||
            lfs_file_size(fs,&file)!=(lfs_soff_t)(sizeof header+(our+peer)*sec_size+cccd*cccd_size))
            rc=LFS_ERR_CORRUPT;
    }
    for (int type=BLE_STORE_OBJ_TYPE_OUR_SEC;!rc && type<=BLE_STORE_OBJ_TYPE_CCCD;++type) {
        unsigned size,capacity,count,used=type==BLE_STORE_OBJ_TYPE_OUR_SEC ? our :
            type==BLE_STORE_OBJ_TYPE_PEER_SEC ? peer : cccd;
        uint8_t *table=records(type,&size,&capacity,&count);
        for (unsigned i=0;!rc && i<used;++i) {
            union ble_store_value value={0};
            size_t bytes=type==BLE_STORE_OBJ_TYPE_CCCD ? sizeof value.cccd : sizeof value.sec;
            n=lfs_file_read(fs,&file,&value,bytes);
            if (n<0) rc=n;
            else if ((size_t)n!=bytes) rc=LFS_ERR_CORRUPT;
            else encode(type,table+i*size,&value);
        }
    }
    int close=lfs_file_close(fs,&file);
    if (rc || close) return rc ? rc : close;
    store->blob[OUR]=our; store->blob[PEER]=peer; store->blob[SUBS]=cccd;
    return 0;
}
int ble_store_littlefs_init(BleStoreLittlefs *s, lfs_t *fs)
{
    store=s;
    ble_hs_cfg.store_read_cb=read_cb; ble_hs_cfg.store_write_cb=write_cb;
    ble_hs_cfg.store_delete_cb=delete_cb; ble_hs_cfg.store_gen_key_cb=gen_key;
    int rc=ble_bond_init(s,fs);
    if (rc) return BLE_HS_ESTORE_FAIL;
    if (!s->exists && !s->blob[OUR] && !s->blob[PEER] && !s->blob[SUBS]) {
        rc=import_legacy(fs);
        if (rc) return fail(rc);
    }
    /* Commit migration before any imported key is used. Fresh stores are
     * committed by gen_key after the controller supplies the local IRK. */
    if ((s->blob[7]&1) && !s->exists) return save();
    return 0;
}

int ble_store_littlefs_clear(void)
{
    if (!store || !store->ready) return BLE_HS_ESTORE_FAIL;
    store->blob[OUR]=store->blob[PEER]=store->blob[SUBS]=0;
    memset(store->blob+HEADER,0,sizeof store->blob-HEADER);
    return save();
}
int ble_store_littlefs_ready(void)
{
    return store && store->ready;
}
int ble_store_littlefs_on_sync(void)
{
    if (!ble_store_littlefs_ready()) return BLE_HS_ESTORE_FAIL;
    /* Standard GATT Service Changed, queued for subscribed bonded peers.
     * This is shared by every platform; never rotate the BLE identity just
     * to evade a client's cached attribute handles. */
    ble_svc_gatt_changed(1,0xffff);
    return store->ready ? 0 : BLE_HS_ESTORE_FAIL;
}
void ble_store_littlefs_counts(unsigned *our, unsigned *peer, unsigned *cccd)
{
    *our=store ? store->blob[OUR] : 0;
    *peer=store ? store->blob[PEER] : 0;
    *cccd=store ? store->blob[SUBS] : 0;
}
