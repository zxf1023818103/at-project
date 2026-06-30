/*
 * mcumgr_compat.c - tinycbor v7 reader/writer shim + flat-buffer SMP streamer for mcumgr 0.7.0.
 * See mcumgr_compat.h for the rationale.
 */
#include <string.h>

#include "mcumgr_compat.h"
#include "mgmt/mgmt.h"     /* MGMT_HDR_SIZE, MGMT_ERR_*, struct mgmt_streamer_cfg */
#include "smp/smp.h"       /* struct smp_streamer, smp_process_request_packet */
#include "tinycbor/cbor.h"

#ifndef MCUMGR_BUF_SZ
#define MCUMGR_BUF_SZ 1152     /* holds one SMP packet: 8B hdr + CBOR(img upload chunk + keys) */
#endif

/* Flat SMP buffer. `off` is the trim watermark (mcumgr trims the header / processed requests off
 * the front); `len` is the total valid length from base. */
struct mcumgr_buf {
    uint8_t *base;
    size_t   off;
    size_t   len;
    size_t   cap;
};

static uint8_t s_req_data[MCUMGR_BUF_SZ];
static uint8_t s_rsp_data[MCUMGR_BUF_SZ];
static struct mcumgr_buf s_req = { s_req_data, 0, 0, sizeof s_req_data };
static struct mcumgr_buf s_rsp = { s_rsp_data, 0, 0, sizeof s_rsp_data };

static mcumgr_tx_fn   s_tx;
static void          *s_tx_arg;

/*
 * Response encoder state. mcumgr's handlers encode entries into the *parent* (ctxt->encoder) while
 * smp.c holds the response map open as a separate *child* container. That only produces valid CBOR
 * if every append streams to one sequential sink - which tinycbor v7's flat encoder does NOT do
 * (close_container copies the child's data.ptr back to the parent, truncating the entries the parent
 * wrote -> an empty/garbled map -> the client's CBOR decode fails). So we drive the encoder in v7
 * STREAMING-WRITER mode: all appends (parent or child) go through mcumgr_enc_write, which appends to
 * the response buffer at a running cursor (after the 8-byte header). bytes_written = that cursor.
 */
struct mcumgr_wctx {
    uint8_t *base;   /* response buffer base; the mgmt header occupies [0, MGMT_HDR_SIZE) */
    size_t   pos;    /* next write offset (starts at MGMT_HDR_SIZE) */
    size_t   cap;
};
static struct mcumgr_wctx s_wctx;

/* ---------------------------------------------------------------- CBOR reader -- */
static void
buf_cpy(struct cbor_decoder_reader *r, char *dst, int off, int len)
{
    memcpy(dst, r->buf + off, (size_t)len);
}

void
cbor_buf_reader_init(struct cbor_buf_reader *r, const uint8_t *buffer, int size)
{
    r->r.buf = buffer;
    r->r.message_size = (size_t)size;
    r->r.live = NULL;
    r->r.cpy = buf_cpy;
}

CborError
mcumgr_cbor_parser_init_reader(struct cbor_decoder_reader *r, int flags,
                               CborParser *parser, CborValue *it)
{
    const uint8_t *base;
    size_t len;

    if (r->live) {                        /* SMP: read the live, post-trim payload */
        base = r->live->base + r->live->off;
        len  = r->live->len - r->live->off;
    } else {                              /* cborattr: flat snapshot */
        base = r->buf;
        len  = r->message_size;
    }
    return cbor_parser_init(base, len, (uint32_t)flags, parser, it);
}

/* ---------------------------------------------------------------- CBOR writer -- */
/* The response header occupies bytes [0, MGMT_HDR_SIZE); the encoder writes the CBOR payload
 * starting right after it. bytes_written is then (encoder cursor - response base). */
static CborError
mcumgr_enc_write(void *token, const void *data, size_t len, CborEncoderAppendType t)
{
    struct mcumgr_wctx *w = token;
    (void)t;
    if (w->pos + len > w->cap) {
        return CborErrorOutOfMemory;
    }
    memcpy(w->base + w->pos, data, len);
    w->pos += len;
    return CborNoError;
}

void
mcumgr_cbor_encoder_init_writer(CborEncoder *enc, struct cbor_encoder_writer *w, int flags)
{
    (void)flags;
    s_wctx.base = w->out->base;
    s_wctx.pos = MGMT_HDR_SIZE;            /* reserve the 8-byte mgmt header; payload follows */
    s_wctx.cap = w->out->cap;
    cbor_encoder_init_writer(enc, mcumgr_enc_write, &s_wctx);
}

size_t
mcumgr_cbor_bytes_written(const CborEncoder *enc)
{
    (void)enc;
    return s_wctx.pos;                      /* total response length = header + payload */
}

/* ------------------------------------------------------- mgmt_streamer_cfg cbs -- */
static void *
cb_alloc_rsp(const void *src, void *arg)
{
    (void)src; (void)arg;
    s_rsp.off = 0;
    s_rsp.len = 0;
    return &s_rsp;
}

static void
cb_trim_front(void *buf, size_t len, void *arg)
{
    struct mcumgr_buf *b = buf;
    (void)arg;
    b->off += len;
    if (b->off > b->len) {
        b->off = b->len;
    }
}

static void
cb_reset_buf(void *buf, void *arg)
{
    struct mcumgr_buf *b = buf;
    (void)arg;
    b->off = 0;
    b->len = 0;
}

static int
cb_write_at(struct cbor_encoder_writer *w, size_t off, const void *data, size_t len, void *arg)
{
    (void)arg;
    if (off + len > w->out->cap) {
        return MGMT_ERR_ENOMEM;
    }
    memcpy(w->out->base + off, data, len);
    if (off + len > w->out->len) {
        w->out->len = off + len;
    }
    return 0;
}

static int
cb_init_reader(struct cbor_decoder_reader *reader, void *buf, void *arg)
{
    struct mcumgr_buf *b = buf;
    (void)arg;
    reader->live = b;
    reader->buf = b->base + b->off;          /* pre-trim header read uses this */
    reader->message_size = b->len - b->off;
    reader->cpy = buf_cpy;
    return 0;
}

static int
cb_init_writer(struct cbor_encoder_writer *writer, void *buf, void *arg)
{
    (void)arg;
    writer->out = (struct mcumgr_buf *)buf;
    return 0;
}

static void
cb_free_buf(void *buf, void *arg)
{
    (void)buf; (void)arg;   /* static buffers; nothing to free */
}

static struct cbor_decoder_reader s_reader;
static struct cbor_encoder_writer s_writer;

static const struct mgmt_streamer_cfg s_cfg = {
    .alloc_rsp   = cb_alloc_rsp,
    .trim_front  = cb_trim_front,
    .reset_buf   = cb_reset_buf,
    .write_at    = cb_write_at,
    .init_reader = cb_init_reader,
    .init_writer = cb_init_writer,
    .free_buf    = cb_free_buf,
};

static int
tx_rsp(struct smp_streamer *ss, void *buf, void *arg)
{
    struct mcumgr_buf *b = buf;
    uint16_t nh_len;
    size_t total;
    (void)ss; (void)arg;

    /* total = 8-byte header + payload; nh_len is at header offset 2 in network byte order. */
    nh_len = (uint16_t)((b->base[2] << 8) | b->base[3]);
    total = MGMT_HDR_SIZE + nh_len;
    if (total > b->cap) {
        total = b->cap;
    }
    return s_tx ? s_tx(s_tx_arg, b->base, total) : 0;
}

static struct smp_streamer s_streamer = {
    .mgmt_stmr = { .cfg = &s_cfg, .cb_arg = NULL, .reader = &s_reader, .writer = &s_writer },
    .tx_rsp_cb = tx_rsp,
};

int
mcumgr_smp_process(const uint8_t *req, size_t req_len, mcumgr_tx_fn tx, void *arg)
{
    if (req_len > sizeof s_req_data) {
        return MGMT_ERR_ENOMEM;
    }
    memcpy(s_req_data, req, req_len);
    s_req.off = 0;
    s_req.len = req_len;
    s_tx = tx;
    s_tx_arg = arg;
    return smp_process_request_packet(&s_streamer, &s_req);
}

size_t
mcumgr_smp_max_packet(void)
{
    return MCUMGR_BUF_SZ;
}
