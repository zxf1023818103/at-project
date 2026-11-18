/* SPDX-License-Identifier: Apache-2.0
 * Project the common BLE bond database into Bluedroid's in-memory config.
 * Only non-security/profile settings use its legacy NVS namespace. All LE
 * keys are stripped before that namespace is written; ble.bonds is authoritative.
 */
#include "bond_config.h"
#include "ble_bond_store.h"
#include "nvs_lfs.h"
#include "btc/btc_config.h"
#include "stack/btm_api.h"
#include "esp_random.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BleBondStore bonds;
static bool deferred;
static const char *roots[]={"LE_LOCAL_KEY_IRK","LE_LOCAL_KEY_ER","LE_LOCAL_KEY_IR","LE_LOCAL_KEY_DHK"};
static const unsigned root_offset[]={12,32,48,64};
static const char *keys[]={"LE_KEY_PENC","LE_KEY_PID","LE_KEY_PCSRK","LE_KEY_LENC",
    "LE_KEY_LCSRK","LE_KEY_LID","LE_LOCAL_EDIV","LE_LOCAL_RAND"};
unsigned ble_bond_config_commits(void) { return bonds.writes; }
void ble_bond_config_defer(bool defer)
{ btc_config_lock(); deferred=defer; btc_config_unlock(); }
static int fail(int rc)
{ nvs_lfs_latch_error(rc); return ble_bond_fail(&bonds,rc); }
static int nibble(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int getbin(const config_t *c, const char *section, const char *key, void *value, size_t n)
{
    const char *str=config_get_string(c,section,key,NULL);
    if (!str) return 0;
    if (strlen(str)!=2*n) return -1;
    uint8_t *p=value;
    for (size_t i=0;i<n;++i) {
        int a=nibble(str[2*i]), b=nibble(str[2*i+1]);
        if (a<0 || b<0) return -1;
        p[i]=(a<<4)|b;
    }
    return 1;
}
static int setbin(config_t *c, const char *section, const char *key, const void *value, size_t n)
{
    char str[2*sizeof(tBTM_LE_KEY_VALUE)+1];
    if (n>(sizeof str-1)/2) return LFS_ERR_INVAL;
    const uint8_t *p=value;
    const char *hex="0123456789abcdef";
    for (size_t i=0;i<n;++i) { str[2*i]=hex[p[i]>>4]; str[2*i+1]=hex[p[i]&15]; }
    str[2*n]=0;
    config_set_string(c,section,key,str,false);
    const char *saved=config_get_string(c,section,key,NULL);
    return saved && !strcmp(saved,str) ? 0 : LFS_ERR_NOMEM;
}
static void name_from_address(char name[18], const uint8_t address[7])
{
    snprintf(name,18,"%02x:%02x:%02x:%02x:%02x:%02x",address[6],address[5],
             address[4],address[3],address[2],address[1]);
}
static int parse_address(const char *name, uint8_t out[7], int type)
{
    if (strlen(name)!=17 || type<0 || type>3) return 0;
    out[0]=type&1;
    for (unsigned i=0;i<6;++i) {
        int a=nibble(name[3*i]), b=nibble(name[3*i+1]);
        if (a<0 || b<0 || (i<5 && name[3*i+2]!=':')) return 0;
        out[6-i]=(a<<4)|b;
    }
    return 1;
}
static uint8_t *table(int type, unsigned *count)
{ unsigned size,capacity; return ble_bond_records(&bonds,type,&size,&capacity,count); }
static uint8_t *existing(int type, const uint8_t address[7])
{
    int i=ble_bond_find(&bonds,type,address);
    unsigned count;
    return i<0 ? NULL : table(type,&count)+i*BLE_BOND_SEC;
}
static int project(config_t *c)
{
    int rc;
    for (unsigned i=0;i<4;++i) if (bonds.blob[7]&(1u<<i)) {
        rc=setbin(c,"Adapter",roots[i],bonds.blob+root_offset[i],16);
        if (rc) return rc;
    }
    for (int type=1;type<=2;++type) {
        unsigned count;
        const uint8_t *p=table(type,&count);
        for (unsigned i=0;i<bonds.blob[count];++i,p+=BLE_BOND_SEC) {
            char name[18]; name_from_address(name,p);
            int auth=1 | ((p[66]&BLE_BOND_SC) ? 8 : 0) | ((p[66]&BLE_BOND_AUTH) ? 4 : 0);
            config_set_int(c,name,"DevType",config_get_int(c,name,"DevType",0)|2);
            config_set_int(c,name,"AddrType",p[0]);
            config_set_int(c,name,"AuthMode",auth);
            unsigned level=(p[66]&BLE_BOND_AUTH) ? BTM_LE_SEC_AUTHENTICATED : BTM_LE_SEC_UNAUTHENTICATE;
            tBTM_LE_KEY_VALUE v={0};
            if (p[66]&BLE_BOND_LTK) {
                if (type==BLE_BOND_OUR) {
                    memcpy(v.lenc_key.ltk,p+18,16); v.lenc_key.div=p[71]|p[72]<<8;
                    v.lenc_key.key_size=p[7]; v.lenc_key.sec_level=level;
                    rc=setbin(c,name,"LE_KEY_LENC",&v.lenc_key,sizeof v.lenc_key);
                    if (!rc && (p[73]&1)) rc=setbin(c,name,"LE_LOCAL_EDIV",p+8,2);
                    if (!rc && (p[73]&1)) rc=setbin(c,name,"LE_LOCAL_RAND",p+10,8);
                } else {
                    memcpy(v.penc_key.ltk,p+18,16); memcpy(v.penc_key.rand,p+10,8);
                    v.penc_key.ediv=p[8]|p[9]<<8; v.penc_key.key_size=p[7]; v.penc_key.sec_level=level;
                    rc=setbin(c,name,"LE_KEY_PENC",&v.penc_key,sizeof v.penc_key);
                }
                if (rc) return rc;
            }
            if (p[66]&BLE_BOND_IRK) {
                if (type==BLE_BOND_OUR) rc=setbin(c,name,"LE_KEY_LID",NULL,0);
                else {
                    memset(&v,0,sizeof v); memcpy(v.pid_key.irk,p+34,16);
                    v.pid_key.addr_type=p[0];
                    for (unsigned j=0;j<6;++j) v.pid_key.static_addr[j]=p[6-j];
                    rc=setbin(c,name,"LE_KEY_PID",&v.pid_key,sizeof v.pid_key);
                }
                if (rc) return rc;
            }
            if (p[66]&BLE_BOND_CSRK) {
                memset(&v,0,sizeof v);
                if (type==BLE_BOND_OUR) {
                    memcpy(v.lcsrk_key.csrk,p+50,16); v.lcsrk_key.counter=ble_bond_get32(p+67);
                    v.lcsrk_key.div=p[71]|p[72]<<8; v.lcsrk_key.sec_level=level;
                    rc=setbin(c,name,"LE_KEY_LCSRK",&v.lcsrk_key,sizeof v.lcsrk_key);
                } else {
                    memcpy(v.pcsrk_key.csrk,p+50,16); v.pcsrk_key.counter=ble_bond_get32(p+67);
                    v.pcsrk_key.sec_level=level;
                    rc=setbin(c,name,"LE_KEY_PCSRK",&v.pcsrk_key,sizeof v.pcsrk_key);
                }
                if (rc) return rc;
            }
        }
    }
    return 0;
}
/* config_remove_key can free the last entry and its section. Copy its name
 * before mutation and re-enumerate; never retain an invalid iterator/name. */
static void strip(config_t *c)
{
    for (unsigned i=0;i<4;++i) config_remove_key(c,"Adapter",roots[i]);
    for (unsigned index=0;;) {
        const config_section_node_t *iter=config_section_begin(c);
        for (unsigned j=0;j<index && iter!=config_section_end(c);++j) iter=config_section_next(iter);
        if (iter==config_section_end(c)) break;
        const char *section=config_section_name(iter);
        uint8_t address[7];
        if (!parse_address(section,address,0)) { ++index; continue; }
        char name[18]; memcpy(name,section,sizeof name);
        for (unsigned j=0;j<sizeof keys/sizeof keys[0];++j) config_remove_key(c,name,keys[j]);
        config_remove_key(c,name,"AuthMode"); config_remove_key(c,name,"AddrType");
        int dev_type=config_get_int(c,name,"DevType",0)&~2;
        if (dev_type) config_set_int(c,name,"DevType",dev_type);
        else config_remove_key(c,name,"DevType");
        if (config_has_section(c,name)) ++index;
    }
}
static int collect_into(const config_t *c, int merge, BleBondStore *dest)
{
    for (unsigned i=0;i<4;++i) {
        uint8_t key[16]; int rc=getbin(c,"Adapter",roots[i],key,sizeof key);
        if (rc<0) return LFS_ERR_CORRUPT;
        if (rc && (!merge || !(dest->blob[7]&(1u<<i)))) {
            memcpy(dest->blob+root_offset[i],key,16); dest->blob[7]|=1u<<i;
        }
        /* Clearing bonds must not rotate local identity roots. */
    }
    for (const config_section_node_t *it=config_section_begin(c);it!=config_section_end(c);it=config_section_next(it)) {
        const char *name=config_section_name(it);
        uint8_t address[7];
        if (!parse_address(name,address,config_get_int(c,name,"AddrType",0))) continue;
        /* A pseudo bond uses another local identity and cannot be represented
         * by the single-identity store. Do not silently mix its keys. */
        if (config_get_int(c,name,"PseudoBond",0)) return LFS_ERR_INVAL;
        tBTM_LE_PID_KEYS pid={0};
        int has_pid=getbin(c,name,"LE_KEY_PID",&pid,sizeof pid);
        if (has_pid<0 || (has_pid && pid.addr_type>3)) return LFS_ERR_CORRUPT;
        if (has_pid) {
            address[0]=pid.addr_type&1;
            for (unsigned j=0;j<6;++j) address[1+j]=pid.static_addr[5-j];
        }
        unsigned auth=config_get_int(c,name,"AuthMode",1);
        for (int type=1;type<=2;++type) {
            uint8_t record[BLE_BOND_SEC]={0};
            uint8_t *old=existing(type,address);
            if (merge && old) continue; /* Common/NimBLE identity wins conflicts. */
            if (old) memcpy(record,old,sizeof record);
            memcpy(record,address,7); record[66]=(auth&8) ? BLE_BOND_SC : 0;
            tBTM_LE_KEY_VALUE v={0}; int rc;
            if (type==BLE_BOND_OUR) {
                rc=getbin(c,name,"LE_KEY_LENC",&v.lenc_key,sizeof v.lenc_key);
                if (rc<0) return LFS_ERR_CORRUPT;
                if (rc) {
                    memcpy(record+18,v.lenc_key.ltk,16); record[7]=v.lenc_key.key_size;
                    record[71]=v.lenc_key.div; record[72]=v.lenc_key.div>>8;
                    record[66]|=BLE_BOND_LTK | (v.lenc_key.sec_level==BTM_LE_SEC_AUTHENTICATED ? BLE_BOND_AUTH : 0);
                    int ediv=getbin(c,name,"LE_LOCAL_EDIV",record+8,2);
                    int rand=getbin(c,name,"LE_LOCAL_RAND",record+10,8);
                    if (ediv<0 || rand<0 || ediv!=rand) return LFS_ERR_CORRUPT;
                    if (ediv) record[73]|=1;
                    if (auth&8) { memset(record+8,0,10); record[73]|=1; }
                }
                if (config_has_key(c,name,"LE_KEY_LID")) {
                    record[66]|=BLE_BOND_IRK; memcpy(record+34,dest->blob+12,16);
                }
                rc=getbin(c,name,"LE_KEY_LCSRK",&v.lcsrk_key,sizeof v.lcsrk_key);
                if (rc<0) return LFS_ERR_CORRUPT;
                if (rc) {
                    record[66]|=BLE_BOND_CSRK; memcpy(record+50,v.lcsrk_key.csrk,16);
                    ble_bond_put32(record+67,v.lcsrk_key.counter);
                    record[71]=v.lcsrk_key.div; record[72]=v.lcsrk_key.div>>8;
                }
            } else {
                rc=getbin(c,name,"LE_KEY_PENC",&v.penc_key,sizeof v.penc_key);
                if (rc<0) return LFS_ERR_CORRUPT;
                if (rc) {
                    record[66]|=BLE_BOND_LTK | (v.penc_key.sec_level==BTM_LE_SEC_AUTHENTICATED ? BLE_BOND_AUTH : 0);
                    memcpy(record+18,v.penc_key.ltk,16); record[7]=v.penc_key.key_size;
                    record[8]=v.penc_key.ediv; record[9]=v.penc_key.ediv>>8; memcpy(record+10,v.penc_key.rand,8);
                }
                if (has_pid) { record[66]|=BLE_BOND_IRK; memcpy(record+34,pid.irk,16); }
                rc=getbin(c,name,"LE_KEY_PCSRK",&v.pcsrk_key,sizeof v.pcsrk_key);
                if (rc<0) return LFS_ERR_CORRUPT;
                if (rc) { record[66]|=BLE_BOND_CSRK; memcpy(record+50,v.pcsrk_key.csrk,16); ble_bond_put32(record+67,v.pcsrk_key.counter); }
            }
            if (!(record[66]&7)) continue;
            if ((record[66]&BLE_BOND_LTK) && (record[7]<7 || record[7]>16)) return LFS_ERR_CORRUPT;
            rc=ble_bond_put(dest,type,record);
            if (rc) return rc;
        }
    }
    /* SC has one shared LTK. Bluedroid may expose only PENC depending on role;
     * NimBLE requires the corresponding OUR_SEC to answer an LTK request. */
    unsigned count,size,cap; uint8_t *p=ble_bond_records(dest,BLE_BOND_PEER,&size,&cap,&count);
    for (unsigned i=0;i<dest->blob[count];++i,p+=BLE_BOND_SEC) {
        int index=ble_bond_find(dest,BLE_BOND_OUR,p);
        uint8_t *our=index<0 ? NULL : dest->blob+BLE_BOND_HEADER+index*BLE_BOND_SEC;
        if ((p[66]&(BLE_BOND_SC|BLE_BOND_LTK))==(BLE_BOND_SC|BLE_BOND_LTK) && (!our || !(our[66]&BLE_BOND_LTK))) {
            uint8_t record[BLE_BOND_SEC]={0};
            if (our) memcpy(record,our,sizeof record);
            memcpy(record,p,34); record[66]|=(p[66]&(BLE_BOND_SC|BLE_BOND_AUTH|BLE_BOND_LTK));
            record[73]|=1;
            int rc=ble_bond_put(dest,BLE_BOND_OUR,record); if (rc) return rc;
        }
    }
    return 0;
}
static int collect(const config_t *c, int merge)
{
    if (merge) return collect_into(c,1,&bonds);
    /* Build the complete next snapshot before replacing RAM/disk state. This
     * also permits an evicted peer to be replaced when the table was full. */
    BleBondStore *next=malloc(sizeof *next);
    if (!next) return LFS_ERR_NOMEM;
    memcpy(next,&bonds,sizeof *next);
    next->blob[8]=next->blob[9]=0;
    memset(next->blob+BLE_BOND_HEADER,0,2*BLE_BOND_MAX_BONDS*BLE_BOND_SEC);
    int rc=collect_into(c,0,next);
    if (!rc) {
        unsigned size,cap,count;
        uint8_t *subs=ble_bond_records(next,BLE_BOND_SUB,&size,&cap,&count);
        for (unsigned i=next->blob[count];i--;) {
            uint8_t *p=subs+i*size;
            if (ble_bond_find(next,BLE_BOND_OUR,p)<0 && ble_bond_find(next,BLE_BOND_PEER,p)<0)
                ble_bond_remove(next,BLE_BOND_SUB,i);
        }
        /* The config list is most-recent-first; serialization has a stable
         * identity order so re-projection never causes redundant flash writes. */
        for (int type=1;type<=2;++type) {
            uint8_t *rows=ble_bond_records(next,type,&size,&cap,&count);
            for (unsigned i=1;i<next->blob[count];++i) {
                uint8_t row[BLE_BOND_SEC]; memcpy(row,rows+i*size,size);
                unsigned j=i;
                while (j && memcmp(row,rows+(j-1)*size,7)<0) {
                    memcpy(rows+j*size,rows+(j-1)*size,size); --j;
                }
                memcpy(rows+j*size,row,size);
            }
        }
        memcpy(bonds.blob,next->blob,sizeof bonds.blob);
    }
    volatile uint8_t *wipe=(volatile uint8_t *)next;
    for (size_t i=0;i<sizeof *next;++i) wipe[i]=0;
    free(next);
    return rc;
}

config_t *ble_bond_config_new(const char *filename)
{
    deferred=false;
    config_t *c=config_new(filename);
    if (!c) return NULL;
    int rc=ble_bond_init(&bonds,vfs_lfs_mount());
    if (!rc && !(bonds.blob[80]&1)) {
        rc=collect(c,1);
        if (!rc) bonds.blob[80]|=1;
    }
    if (!rc) {
        /* Missing Bluedroid roots are independent random roots; never derive a
         * replacement IRK over the NimBLE identity already known to the phone. */
        for (unsigned i=0;i<4;++i) if (!(bonds.blob[7]&(1u<<i))) {
            esp_fill_random(bonds.blob+root_offset[i],16); bonds.blob[7]|=1u<<i;
        }
        rc=ble_bond_save(&bonds);
    }
    if (!rc) { strip(c); rc=project(c); }
    if (rc) { fail(rc); config_free(c); return NULL; }
    return c;
}
bool ble_bond_config_save(const config_t *config, const char *filename)
{
    if (!bonds.ready || nvs_lfs_error()) return false;
    if (deferred) return true;
    int rc=collect(config,0);
    if (!rc) rc=ble_bond_save(&bonds);
    if (rc) { fail(rc); return false; }
    config_t *c=(config_t *)config;
    strip(c);
    bool ok=config_save(c,filename);
    rc=project(c);
    if (rc) fail(rc);
    return ok && !rc;
}
void ble_bond_config_capture_lenc(const uint8_t address[6], uint16_t ediv, const uint8_t rand[8])
{
    uint8_t a[7]={0}, encoded[2]={(uint8_t)ediv,(uint8_t)(ediv>>8)};
    for (unsigned i=0;i<6;++i) a[1+i]=address[5-i];
    char name[18]; name_from_address(name,a);
    /* Called by the SMP task before local key distribution; commit is deferred
     * until the successful authentication event publishes the whole bond. */
    btc_config_lock();
    btc_config_set_bin(name,"LE_LOCAL_EDIV",encoded,2);
    btc_config_set_bin(name,"LE_LOCAL_RAND",rand,8);
    btc_config_unlock();
}
