/* Actual Bluedroid types, config parser and adapter; all keys synthetic. */
#include "../../bluedroid_config/bond_config.c"
#include <assert.h>
#include <stdarg.h>
static config_t *active;
void *osi_malloc_func(size_t n) { return malloc(n); }
void *osi_calloc_func(size_t n) { return calloc(1,n); }
void osi_free_func(void *p) { free(p); }
char *osi_strdup(const char *s) { return strdup(s); }
void esp_fill_random(void *p,size_t n) { memset(p,0x57,n); }
void btc_config_lock(void) { }
void btc_config_unlock(void) { }
bool btc_config_set_bin(const char *section,const char *key,const uint8_t *p,size_t n)
{ return !setbin(active,section,key,p,n); }
void esp_log_write(esp_log_level_t level,const char *tag,const char *fmt,...) { (void)level;(void)tag;(void)fmt; }
uint32_t esp_log_timestamp(void) { return 0; }
void interop_bd_open(void)
{
    active=ble_bond_config_new("bt_config.conf"); assert(active);
    tBTM_LE_LENC_KEYS our={0}; tBTM_LE_PENC_KEYS peer={0}; tBTM_LE_PID_KEYS pid={0};
    assert(getbin(active,"c1:00:00:00:00:01","LE_KEY_LENC",&our,sizeof our)==1);
    assert(getbin(active,"c1:00:00:00:00:01","LE_KEY_PENC",&peer,sizeof peer)==1);
    assert(getbin(active,"c1:00:00:00:00:01","LE_KEY_PID",&pid,sizeof pid)==1);
    for(unsigned i=0;i<16;++i) assert(our.ltk[i]==0x81 && peer.ltk[i]==0x81 && pid.irk[i]==1 && bonds.blob[12+i]==0x6b);
    assert(pid.static_addr[0]==0xc1 && pid.static_addr[5]==1 && pid.addr_type==1);
    assert(our.key_size==16 && peer.key_size==16 && peer.ediv==0);
    assert(config_get_int(active,"c1:00:00:00:00:01","AuthMode",0)==9);
    assert(ble_bond_config_save(active,"bt_config.conf"));
    unsigned writes=bonds.writes;
    assert(ble_bond_config_save(active,"bt_config.conf") && bonds.writes==writes);
}
void interop_bd_add(int legacy)
{
    const char *name=legacy ? "c2:00:00:00:00:03" : "c2:00:00:00:00:02";
    config_set_int(active,name,"DevType",2); config_set_int(active,name,"AddrType",1);
    config_set_int(active,name,"AuthMode",legacy ? 5 : 9);
    tBTM_LE_PENC_KEYS peer={0}; memset(peer.ltk,0x22+legacy,16);
    peer.key_size=16; peer.sec_level=legacy ? BTM_LE_SEC_AUTHENTICATED : BTM_LE_SEC_UNAUTHENTICATE;
    assert(!setbin(active,name,"LE_KEY_PENC",&peer,sizeof peer));
    tBTM_LE_PID_KEYS pid={0}; memset(pid.irk,0x33,16); pid.addr_type=1;
    pid.static_addr[0]=0xc2; pid.static_addr[5]=2+legacy;
    assert(!setbin(active,name,"LE_KEY_PID",&pid,sizeof pid));
    assert(!setbin(active,name,"LE_KEY_LID",NULL,0));
    if(legacy) {
        tBTM_LE_LENC_KEYS our={0}; memset(our.ltk,0x24,16); our.div=0xabc;
        our.key_size=16; our.sec_level=BTM_LE_SEC_AUTHENTICATED;
        assert(!setbin(active,name,"LE_KEY_LENC",&our,sizeof our));
        const uint8_t address[]={0xc2,0,0,0,0,3}, rand[]={1,2,3,4,5,6,7,8};
        ble_bond_config_capture_lenc(address,0xabcd,rand);
    }
    tBTM_LE_PCSRK_KEYS csrk={0}; memset(csrk.csrk,0x44,16); csrk.counter=0x12345678;
    assert(!setbin(active,name,"LE_KEY_PCSRK",&csrk,sizeof csrk));
    config_set_string(active,"Adapter","Name","profile setting survives",false);
    ble_bond_config_defer(true);
    unsigned writes=bonds.writes;
    assert(ble_bond_config_save(active,"bt_config.conf") && bonds.writes==writes);
    ble_bond_config_defer(false);
    assert(ble_bond_config_save(active,"bt_config.conf"));
    config_t *raw=config_new("bt_config.conf"); assert(raw);
    assert(!config_has_key(raw,name,"LE_KEY_PENC") && !config_has_key(raw,"Adapter","LE_LOCAL_KEY_IRK"));
    assert(!strcmp(config_get_string(raw,"Adapter","Name",""),"profile setting survives"));
    config_free(raw);
}
void interop_bd_close(void) { config_free(active); active=NULL; }
#include "btm_int.h"
#include "smp_int.h"
static tBTM_SEC_DEV_REC smp_peer;
static unsigned smp_clears, smp_completions;
tBTM_SEC_DEV_REC *btm_find_dev(BD_ADDR address) { (void)address; return &smp_peer; }
void btm_sec_clear_ble_keys(tBTM_SEC_DEV_REC *peer) { ++smp_clears; peer->ble.key_type=0; }
void smp_sm_event(tSMP_CB *cb,tSMP_EVENT event,void *data)
{ (void)cb; assert(event==SMP_AUTH_CMPL_EVT); assert(*(uint8_t *)data==SMP_SUCCESS); ++smp_completions; }
void interop_smp_reconnect(void)
{
    tSMP_CB cb={0}; uint8_t enabled=1; smp_clears=smp_completions=0;
    smp_peer.ble.key_type=BTM_LE_KEY_PENC|BTM_LE_KEY_PID|BTM_LE_KEY_LENC;
    smp_check_auth_req(&cb,(tSMP_INT_DATA *)&enabled);
    assert(!smp_clears && smp_completions==1 && (smp_peer.ble.key_type&BTM_LE_KEY_LENC));
    cb.flags|=SMP_PAIR_FLAG_ENC_AFTER_PAIR;
    smp_check_auth_req(&cb,(tSMP_INT_DATA *)&enabled);
    assert(smp_clears==1 && smp_completions==2 && !smp_peer.ble.key_type);
}
void interop_bd_replace_at_capacity(void)
{
    for(unsigned id=4;id<=5;++id) {
        if(id==5) assert(config_remove_section(active,"c1:00:00:00:00:01"));
        char name[18];snprintf(name,sizeof name,"c2:00:00:00:00:%02x",id);
        config_set_int(active,name,"DevType",2);config_set_int(active,name,"AddrType",1);config_set_int(active,name,"AuthMode",9);
        tBTM_LE_PENC_KEYS peer={0};memset(peer.ltk,id,16);peer.key_size=16;peer.sec_level=BTM_LE_SEC_UNAUTHENTICATE;
        tBTM_LE_PID_KEYS pid={0};memset(pid.irk,id,16);pid.addr_type=1;pid.static_addr[0]=0xc2;pid.static_addr[5]=id;
        assert(!setbin(active,name,"LE_KEY_PENC",&peer,sizeof peer));assert(!setbin(active,name,"LE_KEY_PID",&pid,sizeof pid));
        assert(!setbin(active,name,"LE_KEY_LID",NULL,0));
        assert(ble_bond_config_save(active,"bt_config.conf"));
        assert(bonds.blob[8]==4 && bonds.blob[9]==4);
    }
}
void interop_bd_expect_empty(void)
{
    active=ble_bond_config_new("bt_config.conf");assert(active);
    assert(!bonds.blob[8] && !bonds.blob[9] && !bonds.blob[10]);
    assert(!config_has_key(active,"c2:00:00:00:00:02","LE_KEY_PENC"));
    for(unsigned i=0;i<16;++i) assert(bonds.blob[12+i]==0x6b);
    interop_bd_close();
}
