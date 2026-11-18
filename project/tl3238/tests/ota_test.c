/* SPDX-License-Identifier: Apache-2.0
 * Real image handlers, SMP/CBOR, SHA-256 and storage state machine. Only the
 * physical NOR device is replaced; fail injection includes partial programs.
 */
#include "ota_storage.h"
#include "mcumgr_compat.h"
#include "img_mgmt/img_mgmt.h"
#include "mgmt/mgmt.h"
#include "mbedtls/sha256.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[0x100000], initial[0x100000], completed[0x100000];
static uint8_t old_image[8193], new_image[12313], digest[32];
static unsigned mutation, fail_at, partial;
static uint8_t response[1152];
static size_t response_length;

int tl_ota_flash_read(uint32_t address, void *data, size_t length)
{ assert(address<=sizeof flash && length<=sizeof flash-address); memcpy(data,flash+address,length); return 0; }
static int mutation_fails(void) { return fail_at && ++mutation==fail_at; }
int tl_ota_flash_write(uint32_t address, const void *data, size_t length)
{
    uint32_t inactive=tl_ota.active^TL_OTA_BANK_SIZE;
    const uint8_t *p=data;
    assert(length && (address&255)+length<=256);
    assert((address>=inactive && address+length<=inactive+TL_OTA_META_OFFSET+4096) ||
           (address==tl_ota.active+0x20 && length==1 && *p==0));
    int fail=mutation_fails();
    size_t n=fail ? (partial ? (length+1)/2 : 0) : length;
    for(size_t i=0;i<n;i++) flash[address+i]&=p[i];
    return fail ? -1 : 0;
}
int tl_ota_flash_erase(uint32_t address)
{
    uint32_t inactive=tl_ota.active^TL_OTA_BANK_SIZE;
    assert(!(address&4095) && address>=inactive && address<=inactive+TL_OTA_META_OFFSET);
    int fail=mutation_fails();
    if(!fail || partial) memset(flash+address,255,fail ? 2048 : 4096);
    return fail ? -1 : 0;
}
void tl_ota_flash_yield(void) { }
static void put32(uint8_t *p,uint32_t n)
{ for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8)); }
static void make_image(uint8_t *data,size_t length,uint32_t build)
{
    for(size_t i=0;i<length;i++) data[i]=(uint8_t)(i*73+build*9);
    data[0x15]=data[0x16]=data[0x17]=0;
    put32(data+0x18,length); put32(data+0x20,TL_OTA_BOOT_MARK);
    put32(data+0x28,TL_OTA_TAG); put32(data+0x2c,build); put32(data+0x30,TL_OTA_FORMAT);
}
static void reset(void)
{
    memcpy(flash,initial,sizeof flash); mutation=fail_at=partial=0;
    memset(&g_img_mgmt_state,0,sizeof g_img_mgmt_state);
    assert(!tl_ota_init(0,sizeof flash));
}
static int tx(void *arg,const uint8_t *data,size_t length)
{ (void)arg; assert(length<=sizeof response); memcpy(response,data,length); response_length=length; return 0; }
static void request(unsigned op,unsigned id,const uint8_t *body,size_t length)
{
    uint8_t packet[1152]; assert(length+8<=sizeof packet);
    memcpy(packet,(uint8_t[]){op,0,length>>8,length,0,1,9,id},8);
    memcpy(packet+8,body,length); response_length=0;
    (void)mcumgr_smp_process(packet,length+8,tx,NULL);
    assert(response_length>=9 && response[0]==op+1 && response[5]==1 && response[6]==9 && response[7]==id);
}
static CborValue value(const char *key)
{
    CborParser p; CborValue root,v;
    assert(!cbor_parser_init(response+8,response_length-8,0,&p,&root));
    assert(!cbor_value_map_find_value(&root,key,&v)); return v;
}
static uint64_t integer(const char *key,uint64_t fallback)
{
    CborValue v=value(key); uint64_t n;
    if(!cbor_value_is_valid(&v)) return fallback;
    assert(!cbor_value_get_uint64(&v,&n)); return n;
}
static unsigned upload(uint64_t off,uint64_t size,const uint8_t *data,size_t n,const uint8_t *sha,size_t sha_len)
{
    uint8_t body[1080]; CborEncoder e,m;
    cbor_encoder_init(&e,body,sizeof body,0);
    assert(!cbor_encoder_create_map(&e,&m,CborIndefiniteLength));
    assert(!cbor_encode_text_stringz(&m,"off")); assert(!cbor_encode_uint(&m,off));
    assert(!cbor_encode_text_stringz(&m,"len")); assert(!cbor_encode_uint(&m,size));
    assert(!cbor_encode_text_stringz(&m,"data")); assert(!cbor_encode_byte_string(&m,data,n));
    if(sha) { assert(!cbor_encode_text_stringz(&m,"sha")); assert(!cbor_encode_byte_string(&m,sha,sha_len)); }
    assert(!cbor_encoder_close_container(&e,&m));
    request(2,1,body,cbor_encoder_get_buffer_size(&e,body));
    return integer("rc",0);
}
static unsigned set_state(const uint8_t *sha,size_t n,int confirm)
{
    uint8_t body[100]; CborEncoder e,m;
    cbor_encoder_init(&e,body,sizeof body,0); assert(!cbor_encoder_create_map(&e,&m,2));
    assert(!cbor_encode_text_stringz(&m,"hash")); assert(!cbor_encode_byte_string(&m,sha,n));
    assert(!cbor_encode_text_stringz(&m,"confirm")); assert(!cbor_encode_boolean(&m,confirm));
    assert(!cbor_encoder_close_container(&e,&m));
    request(2,0,body,cbor_encoder_get_buffer_size(&e,body));
    return integer("rc",0);
}
static void complete_upload(void)
{
    for(size_t off=0;off<sizeof new_image;) {
        size_t n=sizeof new_image-off; if(n>512)n=512;
        assert(!upload(off,sizeof new_image,new_image+off,n,off?NULL:digest,32));
        assert(integer("off",0)==off+n); off+=n;
    }
    assert(flash[TL_OTA_BANK_SIZE+0x20]==255 && flash[0x20]==0x4b);
    assert(!memcmp(flash,old_image,sizeof old_image));
}
static int bootable(uint32_t base)
{ return !memcmp(flash+base+0x20,(uint8_t[]){0x4b,0x4e,0x4c,0x54},4); }
static void safe_boot(void)
{
    assert(bootable(0)||bootable(TL_OTA_BANK_SIZE));
    if(bootable(0)) assert(!memcmp(flash,old_image,sizeof old_image));
    if(bootable(TL_OTA_BANK_SIZE)) assert(!memcmp(flash+TL_OTA_BANK_SIZE,new_image,sizeof new_image));
    assert(!memcmp(flash+0xf1000,initial+0xf1000,sizeof flash-0xf1000));
    assert(!memcmp(flash+TL_LFS_BASE,initial+TL_LFS_BASE,TL_LFS_SIZE));
}
int main(void)
{
    make_image(old_image,sizeof old_image,1); make_image(new_image,sizeof new_image,2);
    assert(!mbedtls_sha256(new_image,sizeof new_image,digest,0));
    memset(initial,255,sizeof initial); memcpy(initial,old_image,sizeof old_image);
    memset(initial+0xf1000,0xa5,sizeof initial-0xf1000);
    memset(initial+TL_LFS_BASE,0x5a,TL_LFS_SIZE);
    img_mgmt_register_group(); reset();
    uint8_t empty=0xa0; request(0,0,&empty,1);
    CborValue images=value("images"); assert(cbor_value_is_array(&images));
    assert(upload(0,sizeof new_image,new_image,512,NULL,0)==MGMT_ERR_EINVAL);
    assert(upload(0,sizeof new_image,new_image,512,digest,31)==MGMT_ERR_EINVAL);
    assert(upload(0,0x100000000ull,new_image,512,digest,32)==MGMT_ERR_EINVAL);
    assert(upload(0,sizeof new_image,new_image,10,digest,32)==MGMT_ERR_EINVAL);
    assert(!memcmp(flash,initial,sizeof flash));
    assert(!upload(0,sizeof new_image,new_image,512,digest,32));
    assert(!upload(0,sizeof new_image,new_image,512,digest,32) && integer("off",0)==512);
    assert(!upload(1024,sizeof new_image,new_image+1024,512,NULL,0) && integer("off",0)==512);
    assert(!tl_ota.uploading || tl_ota.offset==512);
    request(2,5,&empty,1); assert(!integer("rc",0));
    reset(); complete_upload(); memcpy(completed,flash,sizeof completed);
    assert(set_state(digest,31,1)==MGMT_ERR_EINVAL);
    assert(set_state(digest,32,0)==MGMT_ERR_ENOTSUP);
    assert(!tl_ota.pending && flash[0x20]==0x4b);
    uint8_t wrong[32]={0}; assert(set_state(wrong,32,1)==MGMT_ERR_EINVAL);
    flash[TL_OTA_BANK_SIZE+500]^=1;
    assert(set_state(digest,32,1)!=0 && flash[0x20]==0x4b && flash[TL_OTA_BANK_SIZE+0x20]==255);
    memcpy(flash,completed,sizeof flash);
    assert(!set_state(digest,32,1)); assert(tl_ota.pending && !bootable(0) && bootable(TL_OTA_BANK_SIZE));
    assert(upload(0,sizeof new_image,new_image,512,digest,32)==MGMT_ERR_EBADSTATE);
    assert(!tl_ota_init(TL_OTA_BANK_SIZE,sizeof flash));
    assert(tl_ota.running.build==2 && !memcmp(tl_ota.running.hash,digest,32));
    /* The next transfer can go from bank 1 back to bank 0. */
    assert(!tl_ota_begin(sizeof new_image,digest,new_image,512));
    for(size_t off=0;off<sizeof new_image;) {
        size_t n=sizeof new_image-off; if(n>499)n=499;
        assert(!tl_ota_write(off,new_image+off,n,off+n==sizeof new_image)); off+=n;
    }
    assert(!tl_ota_activate() && bootable(0) && !bootable(TL_OTA_BANK_SIZE));
    /* Incorrect transfer SHA never leaves a bootable candidate. */
    reset(); assert(!tl_ota_begin(sizeof new_image,wrong,new_image,512));
    for(size_t off=0;off<sizeof new_image;) {
        size_t n=sizeof new_image-off; if(n>512)n=512;
        int rc=tl_ota_write(off,new_image+off,n,off+n==sizeof new_image);
        assert(rc==(off+n==sizeof new_image ? TL_OTA_HASH : 0)); off+=n;
    }
    assert(tl_ota_activate()!=0); safe_boot();
    /* Loss of power at every mutation, before or part-way through the write. */
    unsigned cases=0;
    for(partial=0;partial<2;partial++) {
        unsigned mode=partial;
        for(unsigned fail=1;fail<=65;fail++) {
            reset(); partial=mode; fail_at=fail;
            int rc=tl_ota_begin(sizeof new_image,digest,new_image,512);
            for(size_t off=0;!rc && off<sizeof new_image;) {
                size_t n=sizeof new_image-off; if(n>512)n=512;
                rc=tl_ota_write(off,new_image+off,n,off+n==sizeof new_image); off+=n;
            }
            if(!rc) (void)tl_ota_activate();
            safe_boot(); cases++;
        }
        partial=mode;
    }
    printf("PASS: real SMP upload/state/erase, bounds, resume, SHA rejection, A/B/A, %u interrupted NOR sequences, reserved-region preservation\n",cases);
    return 0;
}
