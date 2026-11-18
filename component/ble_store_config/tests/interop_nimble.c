#include "ble_store_littlefs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t flash[15*4096],read_cache[512],prog_cache[512],lookahead[8];
static struct { lfs_t fs; } lfs;
static BleStoreLittlefs bonds;
struct ble_hs_cfg ble_hs_cfg;
void ble_hs_lock(void) { }
void ble_hs_unlock(void) { }
void ble_hs_log_flat_buf(const void *p,int n) { (void)p;(void)n; }
int ble_hs_hci_rand(void *p,int n) { memset(p,0x6b,n); return 0; }
lfs_t *vfs_lfs_mount(void) { return &lfs.fs; }
static int rd(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *p,lfs_size_t n)
{ assert(b<c->block_count && off+n<=c->block_size);memcpy(p,flash+b*4096+off,n);return 0; }
static int wr(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *p,lfs_size_t n)
{ assert(b<c->block_count && off+n<=c->block_size);for(unsigned i=0;i<n;++i) { uint8_t v=((const uint8_t *)p)[i];assert((flash[b*4096+off+i]&v)==v);flash[b*4096+off+i]&=v;}return 0; }
static int er(const struct lfs_config *c,lfs_block_t b)
{ assert(b<c->block_count);memset(flash+b*4096,255,4096);return 0; }
static int sync_disk(const struct lfs_config *c) { (void)c;return 0; }
static struct lfs_config cfg={.read=rd,.prog=wr,.erase=er,.sync=sync_disk,
    .lock=sync_disk,.unlock=sync_disk,.read_size=256,.prog_size=256,.block_size=4096,
    .block_count=15,.cache_size=512,.lookahead_size=8,.block_cycles=500,
    .read_buffer=read_cache,.prog_buffer=prog_cache,.lookahead_buffer=lookahead};
static void reboot(void)
{ assert(!lfs_unmount(&lfs.fs));memset(&lfs,0,sizeof lfs);assert(!lfs_mount(&lfs.fs,&cfg));assert(!ble_store_littlefs_init(&bonds,&lfs.fs)); }
static void check_irk(void)
{ struct ble_store_gen_key key;assert(!ble_hs_cfg.store_gen_key_cb(BLE_STORE_GEN_KEY_IRK,&key,0));for(unsigned i=0;i<16;++i) assert(key.irk[i]==0x6b); }
static union ble_store_value security(unsigned id,unsigned value)
{ union ble_store_value v={0};v.sec.peer_addr.type=1;v.sec.peer_addr.val[0]=id;v.sec.peer_addr.val[5]=0xc1;v.sec.key_size=16;v.sec.ltk_present=v.sec.irk_present=v.sec.sc=1;memset(v.sec.ltk,value,16);memset(v.sec.irk,id,16);return v; }
void interop_bd_open(void);
void interop_smp_reconnect(void);
void interop_bd_add(int legacy);
void interop_bd_close(void);
void interop_bd_replace_at_capacity(void);
void interop_bd_expect_empty(void);
int main(void)
{
    interop_smp_reconnect();
    memset(flash,255,sizeof flash);
    memset(&lfs,0,sizeof lfs); assert(!lfs_format(&lfs.fs,&cfg)); assert(!lfs_mount(&lfs.fs,&cfg));
    assert(!ble_store_littlefs_init(&bonds,&lfs.fs));
    struct ble_store_gen_key irk;
    assert(!ble_hs_cfg.store_gen_key_cb(BLE_STORE_GEN_KEY_IRK,&irk,0));
    union ble_store_value value=security(1,0x81); value.sec.ediv=0; value.sec.rand_num=0;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_OUR_SEC,&value));
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&value));
    /* Feed the actual former BLS2 file to the other Host, not a new-format
     * fixture. It must import once and keep the rollback file unchanged. */
    uint8_t old[32+2*4*67+16*12]={0}; memcpy(old,"BLS2",4);
    old[4]=1;old[5]=4;old[6]=16;old[7]=1;old[8]=1;old[9]=1;memcpy(old+12,irk.irk,16);
    memcpy(old+32,bonds.blob+BLE_BOND_HEADER,67);
    memcpy(old+32+4*67,bonds.blob+BLE_BOND_HEADER+4*BLE_BOND_SEC,67);
    ble_bond_put32(old+28,lfs_crc(lfs_crc(0xffffffffu,old,28),old+32,sizeof old-32));
    assert(!lfs_blob_replace(&lfs.fs,"/nimble.bonds","/old.tmp",old,sizeof old));
    assert(!lfs_remove(&lfs.fs,BLE_BOND_PATH));
    interop_bd_open(); interop_bd_add(0); interop_bd_add(1); interop_bd_close();
    reboot(); check_irk();
    for(unsigned id=2;id<=3;++id) {
        union ble_store_key key={0}; key.sec.peer_addr.type=1;
        key.sec.peer_addr.val[0]=id;key.sec.peer_addr.val[5]=0xc2;
        struct ble_store_value_sec peer,our;
        assert(!ble_store_read_peer_sec(&key.sec,&peer)); assert(!ble_store_read_our_sec(&key.sec,&our));
        for(unsigned j=0;j<16;++j) assert(peer.ltk[j]==0x20+id && our.ltk[j]==(id==2 ? 0x22 : 0x24));
        assert(peer.csrk_present && peer.csrk[0]==0x44);
        assert(our.sc==(id==2) && our.authenticated==(id==3));
        if(id==3) assert(our.ediv==0xabcd && our.rand_num==0x0807060504030201ull);
        union ble_store_value repeat={0}; repeat.sec=peer;
        assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&repeat));
        int i=ble_bond_find(&bonds,BLE_BOND_PEER,(const uint8_t *)&key.sec.peer_addr);
        assert(i>=0);
        assert(ble_bond_get32(bonds.blob+BLE_BOND_HEADER+4*BLE_BOND_SEC+i*BLE_BOND_SEC+67)==0x12345678);
    }
    uint8_t preserved[sizeof old];assert(!lfs_blob_read_exact(&lfs.fs,"/nimble.bonds",preserved,sizeof preserved));
    assert(!memcmp(old,preserved,sizeof old));
    interop_bd_open(); interop_bd_replace_at_capacity(); interop_bd_close();
    reboot();
    assert(bonds.blob[8]==4 && bonds.blob[9]==4);
    assert(!ble_store_littlefs_clear()); reboot();
    unsigned our,peer,cccd;ble_store_littlefs_counts(&our,&peer,&cccd);assert(!our && !peer && !cccd);
    check_irk(); interop_bd_expect_empty();
    puts("PASS: BLS2 -> Bluedroid -> NimBLE -> Bluedroid; SC/legacy LTK, identity endian, IRK, CSRK counters, atomic pairing, common deletion, no duplicate LE keys in NVS");
    return 0;
}
