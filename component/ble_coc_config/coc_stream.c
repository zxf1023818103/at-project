/* SPDX-License-Identifier: Apache-2.0 */
#include "coc_stream.h"
#include <string.h>

static uint32_t get32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; i++) p[i]=(uint8_t)(v>>(8*i)); }
static int fail(CocStream *s, unsigned reason)
{ s->phase=COC_FAILED; s->error=(uint8_t)reason; return reason; }
void coc_stream_init(CocStream *s) { memset(s, 0, sizeof *s); }
void coc_stream_abort(CocStream *s)
{
    if (!s->error && (s->phase!=COC_IDLE || s->rx_have || s->tx_size))
        (void)fail(s, COC_ABORTED);
}
static void frame(CocStream *s, unsigned type, uint32_t a, uint32_t b, unsigned length)
{
    memcpy(s->tx_header, "L2C1", 4);
    s->tx_header[4]=(uint8_t)type; s->tx_header[5]=2;
    s->tx_header[6]=(uint8_t)length; s->tx_header[7]=(uint8_t)(length>>8);
    put32(s->tx_header+8, a); put32(s->tx_header+12, b);
    s->tx_size=16+length; s->tx_offset=0;
}
static int header(CocStream *s)
{
    const uint8_t *p=s->rx_header;
    unsigned type=p[4], length=p[6] | (unsigned)p[7]<<8;
    if (memcmp(p, "L2C1", 4) || p[5]!=2) return fail(s, COC_BAD_HEADER);
    if (length>COC_STREAM_MAX_DATA) return fail(s, COC_BAD_LENGTH);
    s->rx_length=(uint16_t)length; s->rx_offset=0;
    switch (type) {
    case 1: case 2:
        if (s->phase!=COC_IDLE || s->tx_size) return fail(s, COC_BAD_STATE);
        if (length) return fail(s, COC_BAD_LENGTH);
        break;
    case 3:
        if (s->phase!=COC_RX) return fail(s, COC_BAD_STATE);
        if (!length || get32(p+12)) return fail(s, COC_BAD_LENGTH);
        if (length>UINT32_MAX-s->rx_bytes) return fail(s, COC_OVERFLOW);
        if (get32(p+8)!=s->rx_bytes) s->errors++;
        break;
    case 4:
        if (s->phase!=COC_RX || s->tx_size) return fail(s, COC_BAD_STATE);
        if (length) return fail(s, COC_BAD_LENGTH);
        break;
    case 5:
        if (s->phase!=COC_WAIT_REPORT || s->tx_size) return fail(s, COC_BAD_STATE);
        if (length!=8) return fail(s, COC_BAD_LENGTH);
        break;
    default: return fail(s, COC_BAD_STATE);
    }
    return 0;
}
static int consume(CocStream *s, uint32_t now)
{
    unsigned type=s->rx_header[4];
    uint32_t a=get32(s->rx_header+8), b=get32(s->rx_header+12);
    if (type==1 || type==2) {
        if (a<1 || a>120 || b<1 || b>COC_STREAM_MAX_DATA) return fail(s, COC_BAD_START);
        s->rx_bytes=s->tx_bytes=s->bad=s->errors=0;
        s->seconds=a; s->block=b; s->started=now;
        s->phase=type==1 ? COC_RX : COC_TX;
        memset(type==1 ? &s->upload : &s->download, 0, sizeof(CocResult));
    } else if (type==3) {
        s->rx_bytes+=s->rx_length;
    } else if (type==4) {
        if (a!=s->rx_bytes) s->errors++;
        s->upload=(CocResult){s->rx_bytes, now-s->started, s->bad, s->errors, 0};
        frame(s, 5, s->rx_bytes, s->bad, 8);
        put32(s->tx_header+16, s->upload.milliseconds);
        put32(s->tx_header+20, s->errors);
        s->phase=COC_REPLY;
    } else if (type==5) {
        if (a!=s->tx_bytes || b || get32(s->rx_report+4)) s->errors++;
        s->download=(CocResult){s->tx_bytes, now-s->started, b, s->errors, 0};
        frame(s, 7, s->tx_bytes, s->errors, 0);
        s->phase=COC_REPLY;
    }
    return 0;
}
int coc_stream_feed(CocStream *s, const void *data, size_t length, uint32_t now)
{
    const uint8_t *p=data;
    if (s->error) return s->error;
    while (length) {
        s->activity=now;
        if (s->rx_have<16) {
            size_t n=16-s->rx_have;
            if (n>length) n=length;
            memcpy(s->rx_header+s->rx_have, p, n);
            s->rx_have+=n; p+=n; length-=n;
            if (s->rx_have<16) continue;
            if (header(s)) return s->error;
        }
        size_t n=s->rx_length-s->rx_offset;
        if (n>length) n=length;
        if (s->rx_header[4]==3) {
            for (size_t i=0; i<n; i++)
                if (p[i]!=(s->rx_bytes+s->rx_offset+i)%251) s->bad++;
        } else if (n) {
            memcpy(s->rx_report+s->rx_offset, p, n); /* Only REPORT has a body. */
        }
        s->rx_offset+=n; p+=n; length-=n;
        if (s->rx_offset!=s->rx_length) continue;
        if (consume(s, now)) return s->error;
        s->rx_have=s->rx_length=s->rx_offset=0;
    }
    return 0;
}
int coc_stream_prepare(CocStream *s, uint32_t now)
{
    if (s->error) return s->error;
    if ((s->phase!=COC_IDLE && (uint32_t)(now-s->started)>(s->seconds+20)*1000) ||
        (s->rx_have && (uint32_t)(now-s->activity)>20000)) return fail(s, COC_TIMEOUT);
    if (!s->tx_size && s->phase==COC_TX) {
        if ((uint32_t)(now-s->started)>=s->seconds*1000) {
            frame(s, 4, s->tx_bytes, now-s->started, 0);
            s->phase=COC_WAIT_REPORT;
        } else {
            if (s->block>UINT32_MAX-s->tx_bytes) return fail(s, COC_OVERFLOW);
            frame(s, 3, s->tx_bytes, 0, s->block);
        }
    }
    return 0;
}
size_t coc_stream_peek(const CocStream *s, void *data, size_t capacity)
{
    uint8_t *p=data;
    size_t n=s->tx_size-s->tx_offset;
    if (n>capacity) n=capacity;
    for (size_t i=0; i<n; i++) {
        unsigned offset=s->tx_offset+i;
        p[i]=offset<16 || s->tx_header[4]!=3 ? s->tx_header[offset] :
             (uint8_t)((s->tx_bytes+offset-16)%251);
    }
    return n;
}
void coc_stream_sent(CocStream *s, size_t length)
{
    if (!length || length>(size_t)(s->tx_size-s->tx_offset)) {
        (void)fail(s, COC_BAD_LENGTH); return;
    }
    s->tx_offset+=length;
    if (s->tx_offset!=s->tx_size) return;
    if (s->tx_header[4]==3) s->tx_bytes+=s->tx_size-16;
    else if (s->tx_header[4]==5) { s->upload.complete=1; s->phase=COC_IDLE; }
    else if (s->tx_header[4]==7) { s->download.complete=1; s->phase=COC_IDLE; }
    s->tx_size=s->tx_offset=0;
}
int coc_stream_selftest(void)
{
    CocStream s;
    uint8_t packet[24]={'L','2','C','1',1,2};
    coc_stream_init(&s); put32(packet+8, 1); put32(packet+12, 1024);
    for (unsigned i=0; i<16; i++)
        if (coc_stream_feed(&s, packet+i, 1, 0)) return 1;
    packet[4]=3; packet[6]=8; put32(packet+8, 0); put32(packet+12, 0);
    for (unsigned i=0; i<8; i++) packet[16+i]=i;
    if (coc_stream_feed(&s, packet, sizeof packet, 1)) return 2;
    packet[4]=4; packet[6]=0; put32(packet+8, 8);
    if (coc_stream_feed(&s, packet, 16, 2)) return 3;
    if (coc_stream_peek(&s, packet, sizeof packet)!=24 || packet[4]!=5 ||
        get32(packet+8)!=8 || get32(packet+12) || get32(packet+20)) return 4;
    coc_stream_sent(&s, 24);
    return s.phase!=COC_IDLE || !s.upload.complete;
}
