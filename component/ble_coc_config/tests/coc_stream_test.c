/* SPDX-License-Identifier: Apache-2.0 */
#include "coc_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; i++) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t get32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static size_t frame(uint8_t *p, unsigned type, uint32_t a, uint32_t b, unsigned n)
{
    memcpy(p,"L2C1",4); p[4]=type; p[5]=2; p[6]=n; p[7]=n>>8;
    put32(p+8,a); put32(p+12,b);
    for (unsigned i=0; i<n; i++) p[16+i]=(a+i)%251;
    return 16+n;
}
static void feed_split(CocStream *s, const uint8_t *p, size_t n, unsigned chunk, uint32_t now)
{
    while (n) {
        size_t k=n<chunk ? n : chunk;
        assert(!coc_stream_feed(s,p,k,now)); p+=k; n-=k;
    }
}
static size_t take(CocStream *s, uint8_t *out, unsigned chunk)
{
    size_t total=0;
    while (s->tx_size) {
        uint8_t retry[4112];
        unsigned n=coc_stream_peek(s,out+total,chunk);
        assert(n && n==coc_stream_peek(s,retry,chunk));
        assert(!memcmp(out+total,retry,n)); /* EBUSY retry is immutable. */
        total+=n; coc_stream_sent(s,n); assert(!s->error);
    }
    return total;
}
static void duplex(unsigned split)
{
    CocStream s; uint8_t data[3*4112+32], out[4112];
    coc_stream_init(&s);
    unsigned n=frame(data,1,1,1024,0), bytes=0;
    for (unsigned i=0; i<3; i++) { n+=frame(data+n,3,bytes,0,4096); bytes+=4096; }
    n+=frame(data+n,4,bytes,1000,0);
    feed_split(&s,data,n,split,100);
    assert(!s.upload.complete && s.phase==COC_REPLY);
    assert(take(&s,out,split)==24 && out[4]==5);
    assert(get32(out+8)==bytes && get32(out+12)==0 && get32(out+20)==0);
    assert(s.upload.complete && s.phase==COC_IDLE);
    n=frame(data,2,1,1024,0); feed_split(&s,data,n,split,200);
    for (unsigned block=0; block<5; block++) {
        assert(!coc_stream_prepare(&s,201+block));
        n=take(&s,out,split);
        assert(n==1040 && out[4]==3 && get32(out+8)==block*1024);
        for (unsigned i=16; i<n; i++) assert(out[i]==(block*1024+i-16)%251);
    }
    assert(s.tx_bytes==5120 && s.upload.complete);
    assert(!coc_stream_prepare(&s,1200));
    assert(take(&s,out,split)==16 && out[4]==4 && get32(out+8)==5120);
    assert(s.phase==COC_WAIT_REPORT && !s.download.complete);
    n=frame(data,5,5120,0,8); put32(data+16,1010); put32(data+20,0);
    feed_split(&s,data,n,split,1210);
    assert(!s.download.complete && s.phase==COC_REPLY);
    assert(take(&s,out,split)==16 && out[4]==7 && get32(out+8)==5120 && !get32(out+12));
    assert(s.download.complete && !s.download.errors && s.phase==COC_IDLE);
}
static void invalid(void)
{
    CocStream s; uint8_t p[4113], out[24];
    const unsigned lengths[]={1,8,4097};
    for (unsigned i=0;i<3;i++) {
        coc_stream_init(&s); frame(p,1,1,1024,lengths[i]);
        assert(coc_stream_feed(&s,p,16,0)==COC_BAD_LENGTH);
    }
    coc_stream_init(&s); frame(p,1,0,1024,0);
    assert(coc_stream_feed(&s,p,16,0)==COC_BAD_START);
    coc_stream_init(&s); frame(p,1,1,1024,0);p[5]=1;
    assert(coc_stream_feed(&s,p,16,0)==COC_BAD_HEADER);
    coc_stream_init(&s); frame(p,3,0,0,1);
    assert(coc_stream_feed(&s,p,17,0)==COC_BAD_STATE);
    coc_stream_init(&s); frame(p,1,1,1024,0);feed_split(&s,p,16,1,0);
    frame(p,3,1,0,8); p[16]=0; /* Bad offset plus seven bad pattern bytes. */
    feed_split(&s,p,24,3,1);frame(p,4,9,0,0);feed_split(&s,p,16,1,2);
    assert(take(&s,out,4)==24 && get32(out+12)==7 && get32(out+20)==2);
    assert(s.upload.errors==2 && s.upload.bad==7);
    coc_stream_init(&s);frame(p,1,1,4096,0);feed_split(&s,p,16,1,0);
    s.rx_bytes=UINT32_MAX;frame(p,3,UINT32_MAX,0,1);
    assert(coc_stream_feed(&s,p,16,0)==COC_OVERFLOW);
    coc_stream_init(&s);frame(p,1,1,4096,0);feed_split(&s,p,7,1,100);
    assert(coc_stream_prepare(&s,20101)==COC_TIMEOUT);
    coc_stream_init(&s);frame(p,2,1,1024,0);feed_split(&s,p,16,1,0xfffffff0u);
    assert(!coc_stream_prepare(&s,984)); /* 1000 ms across clock wrap. */
    assert(take(&s,out,8)==16 && out[4]==4);
    assert(coc_stream_prepare(&s,21000)==COC_TIMEOUT && !s.download.complete);
    coc_stream_init(&s);frame(p,1,1,1024,0);feed_split(&s,p,16,1,0);
    coc_stream_abort(&s);assert(s.error==COC_ABORTED && !s.upload.complete);
    /* A wrong download receipt must propagate an error in COMPLETE. */
    coc_stream_init(&s);frame(p,2,1,1024,0);feed_split(&s,p,16,1,0);
    assert(!coc_stream_prepare(&s,1000));take(&s,out,8);
    frame(p,5,1,2,8);put32(p+16,1000);put32(p+20,3);
    feed_split(&s,p,24,2,1001);take(&s,out,8);
    assert(out[4]==7 && get32(out+12)==1 && s.download.errors==1);
}
int main(void)
{
    const unsigned splits[]={1,2,3,7,15,16,17,23,24,245,1024,2048,4112};
    for (unsigned i=0;i<sizeof splits/sizeof *splits;i++) duplex(splits[i]);
    invalid(); assert(!coc_stream_selftest());
    assert(sizeof(CocStream)<256);
    printf("PASS: L2C1 upload/download receipts, 13 split/coalesced sizes, retries, corruption, bounds, timeout, disconnect, wrap; state=%zu B\n",sizeof(CocStream));
}
