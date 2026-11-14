/* SPDX-License-Identifier: Apache-2.0
 * Real LittleFS, bond callbacks, NimBLE read API and OTA state machine over a
 * bounds-checked NOR model. All keys are synthetic fixtures. */
#include "ble_store_littlefs.h"
#include "lfs_storage.h"
#include "ota_storage.h"
#include "ble_gap_priv.h"
#include "mbedtls/sha256.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[TL_FLASH_SIZE], snapshot[TL_FLASH_SIZE], guard[TL_FLASH_SIZE];
static TlLfs lfs;
static BleStoreLittlefs bonds;
static unsigned mutations, cut_at, partial, power_cut, read_error, cut_cases;
static jmp_buf cut;
struct ble_hs_cfg ble_hs_cfg;
void ble_hs_lock(void) { }
void ble_hs_unlock(void) { }
void tl3238_ble_log(const char *format, ...) { (void)format; }
static struct ble_gap_conn_complete forwarded;
static unsigned forwarded_count;
int tl3238_gap_rx_conn_complete(struct ble_gap_conn_complete *evt, uint8_t instance)
{ assert(instance==3); forwarded=*evt; ++forwarded_count; return 87; }
void ble_hs_log_flat_buf(const void *p,int n) { (void)p; (void)n; }
int ble_hs_hci_rand(void *p,int n) { memset(p,0x6b,(size_t)n); return 0; }
int tl_storage_lock(void) { return 0; }
int tl_storage_unlock(void) { return 0; }
void tl_storage_yield(void) { }
static void inside(uint32_t a,size_t n)
{ assert(a>=TL_LFS_BASE && a<=TL_LFS_BASE+TL_LFS_SIZE && n<=TL_LFS_BASE+TL_LFS_SIZE-a); }
int tl_storage_read(uint32_t a,void *p,size_t n)
{ inside(a,n); if (read_error) return LFS_ERR_IO; memcpy(p,flash+a,n); return 0; }
static int mutation(uint32_t a,const uint8_t *p,size_t n,int erase)
{
    ++mutations;
    int fail=cut_at && mutations==cut_at;
    size_t count=fail ? partial ? n/2 : 0 : n;
    if (erase) memset(flash+a,255,count);
    else for(size_t i=0;i<count;++i) { assert((flash[a+i]&p[i])==p[i]); flash[a+i]&=p[i]; }
    if (fail && power_cut) longjmp(cut,1);
    return fail ? LFS_ERR_IO : 0;
}
int tl_storage_prog(uint32_t a,const void *p,size_t n)
{ inside(a,n); assert(n && n<=256-(a%256)); return mutation(a,p,n,0); }
int tl_storage_erase(uint32_t a)
{ inside(a,4096); assert(!(a%4096)); return mutation(a,NULL,4096,1); }

int tl_ota_flash_read(uint32_t a,void *p,size_t n)
{ assert(a<=sizeof flash && n<=sizeof flash-a); memcpy(p,flash+a,n); return 0; }
int tl_ota_flash_write(uint32_t a,const void *p,size_t n)
{
    uint32_t other=tl_ota.active^TL_OTA_BANK_SIZE;
    assert((a>=other && a+n<=other+TL_OTA_META_OFFSET+4096) || (a==tl_ota.active+0x20 && n==1));
    assert(n && n<=256-(a%256));
    const uint8_t *q=p; for(size_t i=0;i<n;++i) flash[a+i]&=q[i]; return 0;
}
int tl_ota_flash_erase(uint32_t a)
{
    uint32_t other=tl_ota.active^TL_OTA_BANK_SIZE;
    assert(!(a%4096) && a>=other && a<=other+TL_OTA_META_OFFSET);
    memset(flash+a,255,4096); return 0;
}
void tl_ota_flash_yield(void) { }
static void reboot(void)
{
    memset(&lfs,0,sizeof lfs); memset(&bonds,0,sizeof bonds);
    assert(!tl_lfs_mount(&lfs)); assert(!ble_store_littlefs_init(&bonds,&lfs.fs));
    assert(!lfs.formatted);
}
static union ble_store_value security(unsigned id,unsigned value)
{
    union ble_store_value v={0};
    v.sec.peer_addr.type=BLE_ADDR_RANDOM; v.sec.peer_addr.val[0]=id; v.sec.peer_addr.val[5]=0xc1;
    v.sec.key_size=16; v.sec.ediv=0x1234; v.sec.rand_num=0x123456789abcdef0ull;
    v.sec.ltk_present=v.sec.irk_present=v.sec.sc=1;
    memset(v.sec.ltk,value,16); memset(v.sec.irk,id,16);
    return v;
}
static union ble_store_key key_for(const union ble_store_value *v)
{ union ble_store_key k={0}; k.sec.peer_addr=v->sec.peer_addr; return k; }
static unsigned read_key(unsigned id)
{
    union ble_store_value v=security(id,0); union ble_store_key k=key_for(&v);
    struct ble_store_value_sec actual;
    assert(!ble_store_read_peer_sec(&k.sec,&actual));
    assert(actual.key_size==16 && actual.sc && actual.ltk_present && actual.irk_present);
    assert(actual.ediv==0x1234 && actual.rand_num==0x123456789abcdef0ull);
    return actual.ltk[0];
}
static void check_guard(void)
{
    assert(!memcmp(flash,guard,TL_LFS_BASE));
    assert(!memcmp(flash+TL_FLASH_BANK,guard+TL_FLASH_BANK,TL_FLASH_SIZE-TL_FLASH_BANK));
}
static void check_irk(void)
{
    struct ble_store_gen_key k;
    unsigned writes=bonds.writes;
    assert(!ble_hs_cfg.store_gen_key_cb(BLE_STORE_GEN_KEY_IRK,&k,BLE_HS_CONN_HANDLE_NONE));
    for(unsigned i=0;i<16;++i) assert(k.irk[i]==0x6b);
    assert(bonds.writes==writes);
}
static void interrupted_updates(int operation)
{
    union ble_store_value v=security(1,0x99); union ble_store_key k=key_for(&v);
    memcpy(snapshot,flash,sizeof flash); mutations=0;
    if (!operation) assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&v));
    else assert(!ble_hs_cfg.store_delete_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&k));
    unsigned total=mutations;
    assert(total);
    for(unsigned at=1;at<=total;++at) for(unsigned half=0;half<2;++half) {
        memcpy(flash,snapshot,sizeof flash); cut_at=0; reboot();
        mutations=0; cut_at=at; partial=half; power_cut=1;
        if (!setjmp(cut)) {
            if (!operation) (void)ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&v);
            else (void)ble_hs_cfg.store_delete_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&k);
        }
        cut_at=power_cut=0; reboot(); check_irk();
        struct ble_store_value_sec actual;
        int rc=ble_store_read_peer_sec(&k.sec,&actual);
        if (!operation) assert(!rc && (actual.ltk[0]==0x11 || actual.ltk[0]==0x99));
        else assert(rc==BLE_HS_ENOENT || (!rc && actual.ltk[0]==0x11));
        assert(read_key(2)==0x22); check_guard(); ++cut_cases;
    }
    memcpy(flash,snapshot,sizeof flash); reboot(); mutations=0; cut_at=1; partial=1;
    assert(ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&v)==BLE_HS_ESTORE_FAIL);
    assert(!bonds.ready && bonds.error);
    struct ble_store_value_sec actual;
    assert(ble_store_read_peer_sec(&k.sec,&actual)==BLE_HS_ESTORE_FAIL);
    cut_at=0; reboot(); assert(read_key(1)==0x11);
    memcpy(flash,snapshot,sizeof flash); reboot();
}
static void put32(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }
static void make_image(uint8_t *p,size_t n,unsigned build)
{
    memset(p,(int)build,n); p[0x15]=p[0x16]=p[0x17]=0;
    put32(p+0x18,n); put32(p+0x20,TL_OTA_BOOT_MARK);
    put32(p+0x28,TL_OTA_TAG); put32(p+0x2c,build); put32(p+0x30,TL_OTA_FORMAT);
}
static void ota_roundtrip(void)
{
    uint8_t image[8193],hash[32];
    memcpy(snapshot,flash,sizeof flash);
    make_image(image,sizeof image,1); memcpy(flash,image,sizeof image);
    memset(flash+TL_OTA_META_OFFSET,255,4096);
    memset(flash+TL_FLASH_BANK,255,4096); memset(flash+TL_FLASH_BANK+TL_OTA_META_OFFSET,255,4096);
    for(unsigned pass=0;pass<2;++pass) {
        uint32_t active=pass ? TL_FLASH_BANK : 0;
        assert(!tl_ota_init(active,sizeof flash));
        make_image(image,sizeof image,pass+2);
        assert(!mbedtls_sha256(image,sizeof image,hash,0));
        assert(!tl_ota_begin(sizeof image,hash,image,52));
        for(size_t off=0;off<sizeof image;) {
            size_t n=sizeof image-off; if(n>513)n=513;
            assert(!tl_ota_write(off,image+off,n,off+n==sizeof image)); off+=n;
        }
        assert(!tl_ota_activate());
        assert(!memcmp(flash+TL_LFS_BASE,snapshot+TL_LFS_BASE,TL_LFS_SIZE));
        reboot(); assert(read_key(1)==0x11 && read_key(2)==0x22); check_irk();
        assert(!tl_ota_init(active^TL_FLASH_BANK,sizeof flash));
    }
    assert(!memcmp(flash+0xf1000,snapshot+0xf1000,0xf000));
    puts("PASS: actual LittleFS bonds preserved across simulated OTA A/B/A");
}
static void expect_connection(struct ble_gap_conn_complete event,
                              const struct ble_gap_conn_complete *expected)
{
    unsigned count=forwarded_count;
    assert(ble_gap_rx_conn_complete(&event,3)==87);
    assert(forwarded_count==count+1 && !memcmp(&forwarded,expected,sizeof forwarded));
}
static void privacy_reconnect(void)
{
    /* Bluetooth Core 6.0, Vol 3, Part H, Appendix D.7 public ah vector.
     * https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-60/out/en/host/security-manager-specification.html
     * IRK/prand/hash are the published test data, never real device keys. */
    const uint8_t irk[16]={0x9b,0x7d,0x39,0x0a,0xa6,0x10,0x10,0x34,
                          0x05,0xad,0xc8,0x57,0xa3,0x34,0x02,0xec};
    struct ble_gap_conn_complete event={.connection_handle=0x23,.role=1,
        .peer_addr_type=BLE_ADDR_RANDOM,.peer_addr={0xaa,0xfb,0x0d,0x94,0x81,0x70},
        .conn_itvl=24,.conn_latency=1,.supervision_timeout=200,.master_clk_acc=2,
        .local_rpa={1,2,3,4,5,0x41}};
    expect_connection(event,&event); /* no bonds: never guess an identity */
    for (unsigned i=1;i<4;++i) {
        union ble_store_value other=security(i,i);
        assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&other));
    }
    union ble_store_value peer=security(4,0x44);
    peer.sec.peer_addr.type=BLE_ADDR_PUBLIC;
    memcpy(peer.sec.irk,irk,sizeof irk);
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&peer));
    reboot(); /* actual persisted IRK at index 3, not just a single-key fixture */
    struct ble_gap_conn_complete expected=event;
    expected.peer_addr_type=BLE_ADDR_PUBLIC_ID;
    memcpy(expected.peer_addr,peer.sec.peer_addr.val,6);
    memcpy(expected.peer_rpa,event.peer_addr,6);
    unsigned writes=bonds.writes;
    expect_connection(event,&expected);
    assert(bonds.writes==writes);

    struct ble_gap_conn_complete invalid=event;
    invalid.peer_addr[0]^=1; expect_connection(invalid,&invalid); /* bad hash */
    invalid=event; invalid.status=0x3e; expect_connection(invalid,&invalid);
    invalid=event; invalid.peer_addr_type=BLE_ADDR_PUBLIC; expect_connection(invalid,&invalid);
    invalid=event; invalid.peer_addr[5]=0x30; expect_connection(invalid,&invalid); /* NRPA */
    invalid=event; invalid.peer_addr[5]=0xf0; expect_connection(invalid,&invalid); /* static */
    expect_connection(expected,&expected); /* Controller already resolved */

    peer.sec.irk[0]^=1;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&peer));
    expect_connection(event,&event); /* wrong IRK cannot select the LTK */
    peer.sec.irk[0]^=1; peer.sec.irk_present=0;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&peer));
    expect_connection(event,&event);
    peer.sec.irk_present=1;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&peer));
    union ble_store_value collision=security(1,0x11);
    memcpy(collision.sec.irk,irk,sizeof irk);
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&collision));
    expect_connection(event,&event); /* ambiguous identities: leave unresolved */

    assert(!ble_store_littlefs_clear());
    peer.sec.peer_addr.type=BLE_ADDR_RANDOM;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&peer));
    reboot(); expected.peer_addr_type=BLE_ADDR_RANDOM_ID;
    expect_connection(event,&expected);
    bonds.ready=0; expect_connection(event,&event); /* failed store */
    reboot(); assert(!ble_store_littlefs_clear());
    puts("PASS: Core ah vector, persisted RPA identity recovery, wrong/ambiguous keys and event preservation");
}
static void legacy_and_cache(void)
{
    assert(!ble_store_littlefs_clear());
    assert(!lfs_remove(&lfs.fs,BLE_LFS_PATH));
    uint8_t old[28+sizeof(struct ble_store_value_sec)+sizeof(struct ble_store_value_cccd)]={0};
    put32(old,0x424c4653u);put32(old+4,1);
    old[8]=sizeof(struct ble_store_value_sec);old[10]=sizeof(struct ble_store_value_cccd);
    old[12]=BLE_LFS_BONDS;old[14]=BLE_LFS_CCCDS;put32(old+20,1);put32(old+24,1);
    union ble_store_value value=security(7,0x77);
    memcpy(old+28,&value.sec,sizeof value.sec);
    struct ble_store_value_cccd sub={.peer_addr=value.sec.peer_addr,.chr_val_handle=42,.flags=1};
    memcpy(old+28+sizeof value.sec,&sub,sizeof sub);
    assert(!ble_lfs_replace(&lfs.fs,"/ble_store.bin","/legacy.tmp",old,sizeof old));
    assert(!ble_store_littlefs_init(&bonds,&lfs.fs));assert(read_key(7)==0x77);
    struct ble_store_gen_key irk;assert(!ble_hs_cfg.store_gen_key_cb(BLE_STORE_GEN_KEY_IRK,&irk,0));
    reboot();assert(read_key(7)==0x77);check_irk();
    uint8_t actual[sizeof old];assert(!ble_lfs_read_exact(&lfs.fs,"/ble_store.bin",actual,sizeof actual));
    assert(!memcmp(actual,old,sizeof old));
    struct ble_store_key_cccd key={.peer_addr=sub.peer_addr,.chr_val_handle=42};
    struct ble_store_value_cccd got;assert(!ble_store_read_cccd(&key,&got) && got.flags==1);
    /* Common platforms use a 512-byte cache, TL3238 uses 256. Same on-disk
     * records must load and update through both mount configurations. */
    uint8_t rc[512],pc[512];assert(!lfs_unmount(&lfs.fs));
    lfs.cfg.cache_size=512;lfs.cfg.read_buffer=rc;lfs.cfg.prog_buffer=pc;
    assert(!lfs_mount(&lfs.fs,&lfs.cfg));assert(!ble_store_littlefs_init(&bonds,&lfs.fs));
    assert(read_key(7)==0x77);check_irk();
    value=security(7,0x88);assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&value));
    assert(!lfs_unmount(&lfs.fs));reboot();assert(read_key(7)==0x88);
    assert(!lfs_remove(&lfs.fs,BLE_LFS_PATH));
    assert(!ble_lfs_replace(&lfs.fs,"/ble_store.bin","/legacy.tmp",old,sizeof old-1));
    assert(ble_store_littlefs_init(&bonds,&lfs.fs)==BLE_HS_ESTORE_FAIL);
    assert(!lfs_remove(&lfs.fs,"/ble_store.bin"));
    puts("PASS: legacy raw-ABI migration, intact original, truncated legacy rejection, 512/256-byte cache interoperability");
}
int main(void)
{
    struct ble_store_key_sec missing={0}; struct ble_store_value_sec missing_value;
    assert(ble_store_read_peer_sec(&missing,&missing_value)==BLE_HS_ENOTSUP);
    memset(flash,0xa5,sizeof flash); memset(flash+TL_LFS_BASE,255,TL_LFS_SIZE);
    memcpy(guard,flash,sizeof flash);
    assert(!tl_lfs_mount(&lfs) && lfs.formatted); check_guard();
    uint8_t page[256]={0};
    assert(lfs.cfg.prog(&lfs.cfg,15,0,page,256)==LFS_ERR_INVAL);
    assert(lfs.cfg.erase(&lfs.cfg,15)==LFS_ERR_INVAL);
    assert(lfs.cfg.read(&lfs.cfg,0,4096,page,256)==LFS_ERR_INVAL);
    assert(lfs.cfg.prog(&lfs.cfg,0,1,page,256)==LFS_ERR_INVAL);
    assert(!ble_store_littlefs_init(&bonds,&lfs.fs));
    union ble_store_value first=security(1,0x11),second=security(2,0x22);
    union ble_store_key key=key_for(&first);
    struct ble_store_value_sec sec;
    assert(ble_store_read_peer_sec(&key.sec,&sec)==BLE_HS_ENOENT);
    struct ble_store_gen_key local;
    assert(!ble_hs_cfg.store_gen_key_cb(BLE_STORE_GEN_KEY_IRK,&local,BLE_HS_CONN_HANDLE_NONE));
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_OUR_SEC,&first));
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&first));
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&second));
    unsigned writes=bonds.writes;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_PEER_SEC,&second) && bonds.writes==writes);
    union ble_store_value sub={0}; sub.cccd.peer_addr=first.sec.peer_addr;
    sub.cccd.chr_val_handle=0x1234; sub.cccd.flags=3; sub.cccd.value_changed=1;
    assert(!ble_hs_cfg.store_write_cb(BLE_STORE_OBJ_TYPE_CCCD,&sub));
    struct ble_store_key_cccd ck={.peer_addr=first.sec.peer_addr,.chr_val_handle=0x1234};
    struct ble_store_value_cccd cv;
    assert(!ble_store_read_cccd(&ck,&cv) && cv.flags==3 && cv.value_changed);
    reboot(); assert(read_key(1)==0x11 && read_key(2)==0x22); check_irk();
    assert(!ble_store_read_cccd(&ck,&cv) && cv.flags==3);
    interrupted_updates(0); interrupted_updates(1);
    for(unsigned i=3;i<=4;++i) { union ble_store_value v=security(i,i); assert(!ble_hs_cfg.store_write_cb(2,&v)); }
    union ble_store_value fifth=security(5,5);
    assert(ble_hs_cfg.store_write_cb(2,&fifth)==BLE_HS_ESTORE_CAP);
    union ble_store_key any={0}; any.sec.peer_addr=*BLE_ADDR_ANY; any.sec.idx=3;
    union ble_store_value result;
    assert(!ble_hs_cfg.store_read_cb(2,&any,&result) && result.sec.peer_addr.val[0]==4);
    any.sec.idx=4; assert(ble_hs_cfg.store_read_cb(2,&any,&result)==BLE_HS_ENOENT);
    for(unsigned i=1;i<BLE_LFS_CCCDS;++i) { sub.cccd.chr_val_handle=i; assert(!ble_hs_cfg.store_write_cb(3,&sub)); }
    sub.cccd.chr_val_handle=400; assert(ble_hs_cfg.store_write_cb(3,&sub)==BLE_HS_ESTORE_CAP);
    assert(ble_hs_cfg.store_read_cb(0x7fff,&any,&result)==BLE_HS_ENOTSUP);
    check_guard();
    memcpy(snapshot,flash,sizeof flash);
    uint8_t blob[BLE_LFS_BLOB_SIZE]; memcpy(blob,bonds.blob,sizeof blob); blob[100]^=1;
    assert(!ble_lfs_replace(&lfs.fs,BLE_LFS_PATH,BLE_LFS_TEMP,blob,sizeof blob));
    memset(&lfs,0,sizeof lfs); assert(!tl_lfs_mount(&lfs) && !lfs.formatted);
    assert(ble_store_littlefs_init(&bonds,&lfs.fs)==BLE_HS_ESTORE_FAIL);
    memcpy(flash,snapshot,sizeof flash); reboot();
    assert(!ble_lfs_replace(&lfs.fs,BLE_LFS_PATH,BLE_LFS_TEMP,blob,17));
    assert(ble_store_littlefs_init(&bonds,&lfs.fs)==BLE_HS_ESTORE_FAIL);
    memcpy(flash,snapshot,sizeof flash); reboot();
    memset(flash+TL_LFS_BASE,255,TL_LFS_SIZE); flash[TL_FLASH_BANK-1]=0;
    memset(&lfs,0,sizeof lfs); mutations=0;
    assert(tl_lfs_mount(&lfs)==LFS_ERR_CORRUPT && mutations==0);
    read_error=1; memset(&lfs,0,sizeof lfs);
    assert(tl_lfs_mount(&lfs)==LFS_ERR_IO && mutations==0); read_error=0;
    memcpy(flash,snapshot,sizeof flash); reboot();
    ota_roundtrip();
    assert(!ble_store_littlefs_clear()); reboot();
    assert(ble_store_read_peer_sec(&key.sec,&sec)==BLE_HS_ENOENT); check_irk();
    privacy_reconnect();
    legacy_and_cache();
    printf("PASS: LittleFS bounds, IRK/LTK/CCCD reload, capacity, corruption, I/O failures and %u abrupt power cuts\n",cut_cases);
    return 0;
}
