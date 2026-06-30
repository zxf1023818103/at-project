/*
 * mcumgr_compat.h - tinycbor v7 compatibility shim for apache/mynewt-mcumgr 0.7.0.
 *
 * mcumgr 0.7.0 is written against the old apache-mynewt-core tinycbor, which exposes a
 * reader/writer CBOR abstraction (struct cbor_decoder_reader / cbor_encoder_writer, the
 * reader-based cbor_parser_init(), the writer-based cbor_encoder_init(), cbor_encode_bytes_
 * written()). This project ships intel/tinycbor v7 (flat-buffer API). The two are symbol-
 * and ABI-incompatible, so we keep ONE tinycbor (v7) and reimplement the small reader/writer
 * surface mcumgr needs on top of it.
 *
 * This header is force-included (CMake -include) into every mcumgr translation unit so the
 * struct definitions + wrapper declarations are visible. Five mcumgr call sites are patched
 * (on our fork) from the colliding cbor_parser_init/cbor_encoder_init/cbor_encode_bytes_written
 * names to the mcumgr_cbor_* wrappers below (a function-like macro can't be used: it would also
 * mangle v7 cbor.h's own 5-arg declaration).
 *
 * SMP buffers are flat (not os_mbuf): one static request + one static response buffer, single
 * connection, processed on the NimBLE host task (single-threaded), so the static encoder-base
 * stash is safe.
 */
#ifndef MCUMGR_COMPAT_H
#define MCUMGR_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include "tinycbor/cbor.h"   /* intel/tinycbor v7 */

#ifdef __cplusplus
extern "C" {
#endif

/* Flat SMP buffer (request or response). Defined in mcumgr_compat.c. */
struct mcumgr_buf;

/*
 * mcumgr's CBOR decode reader, reimplemented over v7 flat buffers. For SMP the reader points at
 * the live request buffer (`live`) so cbor_parser_init sees the payload after the header trim;
 * for cborattr's flat decode it carries a (buf,message_size) snapshot.
 */
struct cbor_decoder_reader {
    const uint8_t *buf;            /* flat base (snapshot; used by cpy + the flat path) */
    size_t         message_size;   /* flat length */
    const struct mcumgr_buf *live; /* SMP: parse the live (post-trim) buffer; NULL for cborattr */
    void (*cpy)(struct cbor_decoder_reader *r, char *dst, int off, int len);
};

/* cborattr's flat reader wrapper: `struct cbor_buf_reader reader; cbor_buf_reader_init(&reader, d, n);` */
struct cbor_buf_reader {
    struct cbor_decoder_reader r;
};
void cbor_buf_reader_init(struct cbor_buf_reader *r, const uint8_t *buffer, int size);

/* mcumgr's CBOR encode writer: just carries the destination SMP response buffer. */
struct cbor_encoder_writer {
    struct mcumgr_buf *out;
};

/* v7 wrappers replacing mcumgr's reader/writer-based init calls (patched call sites on the fork). */
CborError mcumgr_cbor_parser_init_reader(struct cbor_decoder_reader *r, int flags,
                                         CborParser *parser, CborValue *it);
void      mcumgr_cbor_encoder_init_writer(CborEncoder *enc, struct cbor_encoder_writer *w,
                                          int flags);
size_t    mcumgr_cbor_bytes_written(const CborEncoder *enc);

/*
 * Transport entry point. The smp_bt transport reassembles one full SMP request packet, then
 * calls this; `tx` is invoked once per response packet (the transport fragments it into BLE
 * notifications). Returns an MGMT_ERR_[...] code.
 */
typedef int (*mcumgr_tx_fn)(void *arg, const uint8_t *data, size_t len);
int    mcumgr_smp_process(const uint8_t *req, size_t req_len, mcumgr_tx_fn tx, void *arg);
size_t mcumgr_smp_max_packet(void);   /* reassembly capacity; transport rejects larger requests */

#ifdef __cplusplus
}
#endif

#endif /* MCUMGR_COMPAT_H */
