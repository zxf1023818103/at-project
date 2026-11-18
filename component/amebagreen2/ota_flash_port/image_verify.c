/* Validate only load-record structure, returning no decrypted data to NS.
 * AWS PAL authenticates the complete ciphertext before this check; SBOOT
 * authenticates the decrypted executable again at boot. No OTP key is read
 * into RAM: the IPSEC engine selects the two RSIP OTP keys internally. */
#include "image_layout.h"
/* Resolve ROM APIs through this chip's linker map; no Dplus entry points. */
extern uint32_t SYSCFG_OTP_RSIPEn(void);
extern uint32_t SYSCFG_OTP_RSIPMode(void);
extern int crypto_aes_ecb_slave(uint8_t key_id, uint32_t keybits, uint8_t encrypt,
                                const uint8_t input[16], uint8_t output[16]);
struct image_reader { uint32_t base,size; int encrypted; uint8_t iv[8]; };
static int aes(unsigned key, int decrypt, uint8_t input[32], uint8_t output[32])
{
    /* Green2 app RSIP group 1: key-manager IDs 0/1 (OTP 0x200/0x220).
     * IDs 4/5 are the separate bootloader group at 0x2c0/0x2e0. */
    for (unsigned offset = 0; offset < 32; offset += 16) {
        int rc = crypto_aes_ecb_slave(key, 256, !decrypt, input+offset, output+offset);
        if (rc) return rc;
    }
    return 0;
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
    int rc=aes(1,0,counter,mask);
    if (!rc) {
        memcpy(block,(const void *)(0x08000000+r->base+offset),32);
        for (unsigned i=0;i<32;++i) block[i]^=mask[i];
        rc=aes(0,1,block,plain);
        if (!rc) for (unsigned i=0;i<32;++i) out[i]=plain[i]^mask[i];
    }
    return rc;
}
/* Caller holds IPC_SEM_CRYPTO and masks local NS interrupts. In TF-M the
 * gateway has already restricted base to the inactive OTA slot. */
int green2_verify_flash_layout(uint32_t base, uint32_t size, int tfm)
{
    if ((base!=0x10000 && base!=0x190000) || size<0x2100 || size>0x170000) return -1;
    struct image_reader r={.base=base,.size=size};
    r.encrypted=SYSCFG_OTP_RSIPEn()!=0;
    if (r.encrypted && SYSCFG_OTP_RSIPMode()!=0) return -1;
    memcpy(r.iv,(const void *)(0x08000000+base+0x1020),8);
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    int rc=green2_image_layout(read_record,&r,size,tfm);
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
    return rc;
}
