/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_bond_store.h"
#include <stdlib.h>
#include <string.h>
__attribute__((weak)) void *ble_bond_alloc(size_t n) { return malloc(n); }
__attribute__((weak)) void ble_bond_free(void *p) { free(p); }

_Static_assert(BLE_BOND_MAX_BONDS>0 && BLE_BOND_MAX_BONDS<=255 &&
               BLE_BOND_MAX_CCCDS<=255, "store capacities must fit header");
uint32_t ble_bond_get32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
void ble_bond_put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t checksum(const uint8_t *p, size_t n)
{ return lfs_crc(lfs_crc(0xffffffffu,p,28),p+32,n-32); }
int ble_bond_fail(BleBondStore *s, int error)
{ s->ready=0; s->error=error; return error; }
uint8_t *ble_bond_records(BleBondStore *s, int type, unsigned *size,
                         unsigned *capacity, unsigned *count)
{
    if (type==BLE_BOND_OUR || type==BLE_BOND_PEER) {
        *size=BLE_BOND_SEC; *capacity=BLE_BOND_MAX_BONDS; *count=7+type;
        return s->blob+BLE_BOND_HEADER+(type-1)*BLE_BOND_MAX_BONDS*BLE_BOND_SEC;
    }
    if (type==BLE_BOND_SUB) {
        *size=BLE_BOND_CCCD; *capacity=BLE_BOND_MAX_CCCDS; *count=10;
        return s->blob+BLE_BOND_HEADER+2*BLE_BOND_MAX_BONDS*BLE_BOND_SEC;
    }
    return NULL;
}
int ble_bond_find(BleBondStore *s, int type, const uint8_t address[7])
{
    unsigned size,cap,count;
    uint8_t *p=ble_bond_records(s,type,&size,&cap,&count);
    if (!p) return -1;
    for (unsigned i=0;i<s->blob[count];++i)
        if (!memcmp(p+i*size,address,7)) return i;
    return -1;
}
int ble_bond_put(BleBondStore *s, int type, const uint8_t *record)
{
    unsigned size,cap,count;
    uint8_t *p=ble_bond_records(s,type,&size,&cap,&count);
    if (!p || type==BLE_BOND_SUB) return LFS_ERR_INVAL;
    int index=ble_bond_find(s,type,record);
    if (index<0) {
        if (s->blob[count]>=cap) return LFS_ERR_NOSPC;
        index=s->blob[count]++;
    }
    memcpy(p+index*size,record,size);
    return 0;
}
void ble_bond_remove(BleBondStore *s, int type, unsigned index)
{
    unsigned size,cap,count;
    uint8_t *p=ble_bond_records(s,type,&size,&cap,&count);
    if (!p || index>=s->blob[count]) return;
    --s->blob[count];
    memmove(p+index*size,p+(index+1)*size,(s->blob[count]-index)*size);
    memset(p+s->blob[count]*size,0,size);
}
static int validate_live(BleBondStore *s)
{
    if (s->blob[7]>15 || s->blob[11]) return LFS_ERR_CORRUPT;
    for (int type=1;type<=3;++type) {
        unsigned size,cap,count;
        const uint8_t *table=ble_bond_records(s,type,&size,&cap,&count);
        if (s->blob[count]>cap) return LFS_ERR_NOSPC;
        for (unsigned i=0;i<s->blob[count];++i) {
            const uint8_t *p=table+i*size;
            if (p[0]>1) return LFS_ERR_CORRUPT;
            if (type==3) {
                if (!(p[7]|p[8]) || (p[9]&~3u) || p[10] || p[11]>1) return LFS_ERR_CORRUPT;
            } else if (!(p[66]&7) || (p[66]&~63u) ||
                ((p[66]&BLE_BOND_LTK) && (p[7]<7 || p[7]>16))) return LFS_ERR_CORRUPT;
            for (unsigned j=0;j<i;++j) {
                const uint8_t *other=table+j*size;
                if (!memcmp(p,other,7) && (type!=3 ||
                    (!memcmp(p+7,other+7,2) && !memcmp(p+12,other+12,4)))) return LFS_ERR_CORRUPT;
            }
        }
    }
    return 0;
}
int ble_bond_save(BleBondStore *s)
{
    if (!s->ready) return s->error ? s->error : LFS_ERR_IO;
    int validation=validate_live(s);
    if (validation) return ble_bond_fail(s,validation);
    uint32_t crc=checksum(s->blob,sizeof s->blob);
    if (s->exists && ble_bond_get32(s->blob+28)==crc) {
        /* CRC detects damage; byte comparison decides whether a write can be
         * skipped. A collision must never suppress a changed security key. */
        uint8_t *previous=ble_bond_alloc(sizeof s->blob);
        if (!previous) return ble_bond_fail(s,LFS_ERR_NOMEM);
        int rc=lfs_blob_read_exact(s->fs,BLE_BOND_PATH,previous,sizeof s->blob);
        int same=!rc && !memcmp(previous,s->blob,sizeof s->blob);
        volatile uint8_t *wipe=previous;
        for (unsigned i=0;i<sizeof s->blob;++i) wipe[i]=0;
        ble_bond_free(previous);
        if (rc) return ble_bond_fail(s,rc);
        if (same) return 0;
    }
    ble_bond_put32(s->blob+28,crc);
    int rc=lfs_blob_replace(s->fs,BLE_BOND_PATH,BLE_BOND_TEMP,s->blob,sizeof s->blob);
    if (rc) return ble_bond_fail(s,rc);
    ++s->writes; s->exists=1;
    return 0;
}
/* BLS2 and BLS3 share the first 67 bytes of a security record. BLS3 adds
 * CSRK counter (67), Bluedroid DIV (71), metadata flags (73), reserved (74).
 * CCCD records add a database namespace at offset 12: 1 = NimBLE handles.
 * Handles are never replayed into a different Host's GATT database. */
static int decode(BleBondStore *s, const uint8_t *p, size_t n, int old)
{
    unsigned header=old ? 32 : BLE_BOND_HEADER, sec=old ? 67 : BLE_BOND_SEC;
    unsigned sub=old ? 12 : BLE_BOND_CCCD;
    if (n<header || memcmp(p,old ? "BLS2" : "BLS3",4) || p[4]!=1 ||
        !p[5] || p[8]>p[5] || p[9]>p[5] || p[10]>p[6] || p[11] ||
        (old ? p[7]!=1 : p[7]>15) || n!=header+2u*p[5]*sec+p[6]*sub ||
        ble_bond_get32(p+28)!=checksum(p,n)) return LFS_ERR_CORRUPT;
    if (p[8]>BLE_BOND_MAX_BONDS || p[9]>BLE_BOND_MAX_BONDS || p[10]>BLE_BOND_MAX_CCCDS)
        return LFS_ERR_NOSPC;
    memcpy(s->blob+7,p+7,21);
    if (!old) memcpy(s->blob+32,p+32,BLE_BOND_HEADER-32);
    for (int type=1;type<=3;++type) {
        unsigned size,cap,count;
        uint8_t *out=ble_bond_records(s,type,&size,&cap,&count);
        const uint8_t *in=p+header+(type==3 ? 2u*p[5]*sec : (type-1)*p[5]*sec);
        unsigned width=type==3 ? sub : sec;
        for (unsigned i=0;i<p[count];++i,in+=width,out+=size) {
            if (in[0]>1) return LFS_ERR_CORRUPT;
            if (type!=3 && (!(in[66]&7) || (in[66]&~63u) ||
                ((in[66]&BLE_BOND_LTK) && (in[7]<7 || in[7]>16)))) return LFS_ERR_CORRUPT;
            if (type==3 && (!(in[7]|in[8]) || (in[9]&~3u) || in[10] || in[11]>1))
                return LFS_ERR_CORRUPT;
            for (unsigned j=0;j<i;++j) {
                const uint8_t *other=in-(i-j)*width;
                if (!memcmp(in,other,7) && (type!=3 ||
                    (!memcmp(in+7,other+7,2) && (old || !memcmp(in+12,other+12,4)))))
                    return LFS_ERR_CORRUPT;
            }
            memcpy(out,in,width);
            if (old && type==3) ble_bond_put32(out+12,1);
            if (old && type!=3) out[73]|=1;
        }
    }
    return 0;
}
int ble_bond_init(BleBondStore *s, lfs_t *fs)
{
    memset(s,0,sizeof *s); s->fs=fs;
    memcpy(s->blob,"BLS3",4); s->blob[4]=1;
    s->blob[5]=BLE_BOND_MAX_BONDS; s->blob[6]=BLE_BOND_MAX_CCCDS;
    if (!fs) return ble_bond_fail(s,LFS_ERR_IO);
    struct lfs_info info;
    const char *path=BLE_BOND_PATH;
    int rc=lfs_stat(fs,path,&info), old=0;
    if (rc==LFS_ERR_NOENT) { path="/nimble.bonds"; old=1; rc=lfs_stat(fs,path,&info); }
    if (rc==LFS_ERR_NOENT) { s->ready=1; return 0; }
    if (rc) return ble_bond_fail(s,rc);
    if (info.type!=LFS_TYPE_REG || info.size<32 || info.size>96+2*255*80+255*16)
        return ble_bond_fail(s,LFS_ERR_CORRUPT);
    uint8_t *data=ble_bond_alloc(info.size);
    if (!data) return ble_bond_fail(s,LFS_ERR_NOMEM);
    rc=lfs_blob_read_exact(fs,path,data,info.size);
    if (!rc) rc=decode(s,data,info.size,old);
    if (!rc && !old && info.size==sizeof s->blob) {
        s->exists=1; memcpy(s->blob+28,data+28,4);
    }
    volatile uint8_t *wipe=data;
    for (size_t i=0;i<info.size;++i) wipe[i]=0;
    ble_bond_free(data);
    if (rc) return ble_bond_fail(s,rc);
    s->ready=1;
    return 0;
}
