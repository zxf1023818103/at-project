/* Validate only load-record structure, returning no decrypted data to NS.
 * AWS PAL authenticates the complete ciphertext before this check; SBOOT
 * authenticates the decrypted executable again at boot. No OTP key is read
 * into RAM: the IPSEC engine selects the two RSIP OTP keys internally. */
#include "image_layout.h"
#define REG32(a) (*(volatile uint32_t *)(a))
#ifdef DPLUS_IMAGE_SECURE
#define ROM_INIT 0xdadu
#define ROM_KEY 0xdfdu
#define ROM_ECB_INIT 0x1d9du
#define ROM_ENCRYPT 0x1dfdu
#define ROM_DECRYPT 0x1e3du
#define ROM_RSIP_EN 0xc249u
#define ROM_RSIP_MODE 0xc375u
#else
#define ROM_INIT 0x201e5u
#define ROM_KEY 0x2022du
#define ROM_ECB_INIT 0x1ecf5u
#define ROM_ENCRYPT 0x1ed49u
#define ROM_DECRYPT 0x1ed85u
#define ROM_RSIP_EN 0x24ad5u
#define ROM_RSIP_MODE 0x24c01u
#endif
struct image_reader { uint32_t base,size; int encrypted; uint8_t iv[8]; };
static void cache(uint8_t *p, uint32_t reg)
{
    __asm volatile("dsb 0xf" ::: "memory");
    REG32(reg)=(uint32_t)p;
    __asm volatile("dsb 0xf" ::: "memory");
}
static int aes(unsigned key, int decrypt, uint8_t input[32], uint8_t output[32])
{
    uint8_t unused[32] __attribute__((aligned(32)))={0};
    int rc=((int (*)(uint32_t,uint32_t))ROM_KEY)(key,1);
    if (!rc) rc=((int (*)(const uint8_t *,uint32_t))ROM_ECB_INIT)(unused,32);
    cache(input,0xe000ef70); cache(output,0xe000ef70);
    if (!rc) rc=((int (*)(const uint8_t *,uint32_t,const uint8_t *,uint32_t,uint8_t *))
        (decrypt ? ROM_DECRYPT : ROM_ENCRYPT))(input,32,0,0,output);
    cache(output,0xe000ef5c);
    return rc;
}
static int read_record(void *arg, uint32_t offset, uint32_t logical, uint8_t out[32])
{
    struct image_reader *r=arg;
    if (offset>r->size || r->size-offset<32 || (offset&31)) return -1;
    if (!r->encrypted || !logical) {
        memcpy(out,(const void *)(0x08000000+r->base+offset),32); return 0;
    }
    uint8_t counter[32] __attribute__((aligned(32)))={0};
    uint8_t mask[32] __attribute__((aligned(32)));
    uint8_t block[32] __attribute__((aligned(32)));
    uint8_t plain[32] __attribute__((aligned(32)));
    uint32_t address=(logical&0x0fffffffu)/32;
    for (unsigned i=0;i<2;++i) {
        uint8_t *p=counter+16*i;
        memcpy(p,r->iv,8);
        for (unsigned j=0;j<4;++j) p[8+j]=address>>(24-8*j);
        p[15]=2+i;
    }
    int rc=aes(3,0,counter,mask);
    if (!rc) {
        memcpy(block,(const void *)(0x08000000+r->base+offset),32);
        for (unsigned i=0;i<32;++i) block[i]^=mask[i];
        rc=aes(2,1,block,plain);
        if (!rc) for (unsigned i=0;i<32;++i) out[i]=plain[i]^mask[i];
    }
    return rc;
}
/* Caller holds IPC_SEM_CRYPTO and masks local NS interrupts. In TF-M the
 * gateway has already restricted base to the inactive OTA slot. */
int dplus_verify_flash_layout(uint32_t base, uint32_t size, int tfm)
{
    if ((base!=0x10000 && base!=0x190000) || size<0x2100 || size>0x170000) return -1;
    struct image_reader r={.base=base,.size=size};
    r.encrypted=((uint32_t (*)(void))ROM_RSIP_EN)()!=0;
    if (r.encrypted && ((uint32_t (*)(void))ROM_RSIP_MODE)()!=0) return -1;
    memcpy(r.iv,(const void *)(0x08000000+base+0x1020),8);
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    int rc=r.encrypted ? ((int (*)(void *))ROM_INIT)(0) : 0;
    if (!rc) rc=dplus_image_layout(read_record,&r,size,tfm);
    if (r.encrypted) ((int (*)(uint32_t,uint32_t))ROM_KEY)(0,0);
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
    return rc;
}
