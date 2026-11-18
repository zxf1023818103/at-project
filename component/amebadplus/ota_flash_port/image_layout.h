#pragma once
#include <stdint.h>
#include <string.h>
/* One parser for plaintext and OTP-decrypted load records. The reader returns
 * exactly 32 bytes; logical=0 denotes an unencrypted manifest record. */
typedef int (*dplus_image_reader)(void *, uint32_t, uint32_t, uint8_t [32]);
static inline uint32_t dplus_le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static inline int dplus_image_layout(dplus_image_reader read, void *ctx, uint32_t size, int tfm)
{
    uint8_t h[32];
    if (size < 0x2100 || size > 0x170000 ||
        read(ctx,0x1000,0,h) || memcmp(h,"81958711",8) ||
        h[16]!=1 || dplus_le32(h+24)!=size-0x2000) return -1;
    uint32_t pos=0x2000;
    for (unsigned core=0;core<2;++core) {
        uint32_t start=pos, logical=core ? 0x0e000000 : 0x0c000000;
        for (unsigned seg=0;seg<4;++seg) {
            if (pos>size-32 || read(ctx,pos,logical+pos-start,h) || memcmp(h,"81958711",8)) return -1;
            uint32_t n=dplus_le32(h+8), address=dplus_le32(h+12);
            if (n%32 || n>size-pos-32 ||
                (seg==3 && address!=(core ? 0x20004da0u : 0x20004d20u))) return -1;
            pos+=32+n;
        }
        if (!core) pos=(pos+4095)&~4095u;
    }
    if (!tfm) return pos==size ? 0 : -1;
    if (pos>size || size-pos<1056 || read(ctx,pos,0x0f000000,h) ||
        memcmp(h,"81958711",8) || dplus_le32(h+12)!=0x0f000020) return -1;
    uint32_t n=dplus_le32(h+8);
    if (n%32 || n>0x603e0 || n!=size-pos-32 || read(ctx,pos+1024,0x0f000400,h)) return -1;
    uint32_t stack=dplus_le32(h), entry=dplus_le32(h+4);
    return stack>0x3000b000 && stack<=0x30019000 &&
        (entry&1) && entry>=0x0f000400 && entry<0x0f060400 ? 0 : -1;
}
