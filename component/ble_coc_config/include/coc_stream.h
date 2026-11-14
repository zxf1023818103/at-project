/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Stack/RTOS/chip-independent Flash Studio L2C1 stream protocol. */
#define COC_STREAM_HEADER 16u
#define COC_STREAM_MAX_DATA 4096u
enum { COC_IDLE, COC_RX, COC_TX, COC_WAIT_REPORT, COC_REPLY, COC_FAILED };
enum { COC_OK, COC_BAD_HEADER, COC_BAD_STATE, COC_BAD_LENGTH,
       COC_BAD_START, COC_OVERFLOW, COC_TIMEOUT, COC_ABORTED };
typedef struct {
    uint32_t bytes, milliseconds, bad, errors;
    unsigned complete;
} CocResult;
typedef struct {
    uint8_t rx_header[16], rx_report[8], tx_header[24];
    uint16_t rx_have, rx_length, rx_offset, tx_size, tx_offset;
    uint8_t phase, error;
    uint32_t rx_bytes, tx_bytes, bad, errors, block, seconds, started, activity;
    CocResult upload, download;
} CocStream;

void coc_stream_init(CocStream *s);
int coc_stream_feed(CocStream *s, const void *data, size_t length, uint32_t now_ms);
int coc_stream_prepare(CocStream *s, uint32_t now_ms);
size_t coc_stream_peek(const CocStream *s, void *data, size_t capacity);
/* Commit only bytes accepted by the transport, including a stalled SDU. */
void coc_stream_sent(CocStream *s, size_t length);
void coc_stream_abort(CocStream *s);
int coc_stream_selftest(void);
