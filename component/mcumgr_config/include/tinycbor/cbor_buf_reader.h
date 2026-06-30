/*
 * tinycbor/cbor_buf_reader.h - satisfies cborattr.c's unconditional include.
 *
 * The real reader/writer abstraction lives in mcumgr_compat.h (force-included into every mcumgr
 * TU). mcumgr's cbor_mbuf_reader.h / cbor_mbuf_writer.h are only pulled in under #ifdef MYNEWT,
 * which we do not define, so they are never compiled and need no shim.
 */
#ifndef MCUMGR_SHIM_TINYCBOR_CBOR_BUF_READER_H
#define MCUMGR_SHIM_TINYCBOR_CBOR_BUF_READER_H

#include "mcumgr_compat.h"

#endif
