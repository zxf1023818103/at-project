/* Exercise the shared wire protocol with the board's actual libc. Native tests
 * cannot detect a target SDK silently compiling out long long formatting. */
#include "FreeRTOS.h"
#include "nan_tcp_protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#if !defined(CONFIG_VSNPRINTF_LONG_LONG) || !CONFIG_VSNPRINTF_LONG_LONG
#error "NAN throughput JSON requires CONFIG_VSNPRINTF_LONG_LONG=y"
#endif

#define PAYLOAD_BYTES 65536u
#define STREAM_BYTES 66000u
struct test_stream {
    uint8_t input[STREAM_BYTES], output[STREAM_BYTES];
    size_t input_size, read_at, output_size;
    uint32_t now;
    int direction;
};

static int read_bytes(void *context, void *data, size_t size, uint32_t timeout)
{
    struct test_stream *s = context;
    if (!timeout) return -1;
    if (size > s->input_size - s->read_at) size = s->input_size - s->read_at;
    memcpy(data, s->input + s->read_at, size);
    s->read_at += size;
    if (s->direction == GREEN2_NAN_TCP_UPLOAD && s->read_at == s->input_size)
        s->now = 1250;
    return (int)size;
}

static int write_bytes(void *context, const void *data, size_t size, uint32_t timeout)
{
    struct test_stream *s = context;
    if (!timeout || size >= sizeof(s->output) - s->output_size) return -1;
    memcpy(s->output + s->output_size, data, size);
    s->output_size += size;
    if (s->direction == GREEN2_NAN_TCP_DOWNLOAD && s->output_size == 6 + 4 + PAYLOAD_BYTES)
        s->now = 1250;
    return (int)size;
}

static uint32_t now_ms(void *context) { return ((struct test_stream *)context)->now; }

int bl618dg_nan_protocol_selftest(void)
{
    char formatted[96];
    snprintf(formatted, sizeof(formatted), "%" PRIu64 "/%" PRIu64 "/%" PRIu64 ".%03" PRIu64 "/%s",
             UINT64_C(4294967296), UINT64_MAX, UINT64_C(8), UINT64_C(5), "ok");
    if (strcmp(formatted, "4294967296/18446744073709551615/8.005/ok")) {
        puts("[NAN] 64-bit JSON formatting self-test FAIL");
        return -1;
    }
    struct test_stream *s = pvPortMalloc(sizeof(*s));
    if (!s) return -1;
    const struct green2_nan_tcp_io io = {s, read_bytes, write_bytes, now_ms};
    static const uint8_t header[4] = {0, 1, 0, 0}, terminator[4] = {0};
    static const char *const commands[] = {
        "NAN-THROUGHPUT/1 selftest UPLOAD 1\n",
        "NAN-THROUGHPUT/1 selftest DOWNLOAD 1\n"
    };
    static const char *const expected[] = {
        "{\"version\":1,\"test_id\":\"selftest\",\"direction\":\"upload\",\"bytes\":65536,\"seconds\":1.250,\"mbps\":0.419,\"counter\":\"received\"}\n",
        "{\"version\":1,\"test_id\":\"selftest\",\"direction\":\"download\",\"bytes\":65536,\"seconds\":1.250,\"mbps\":0.419,\"counter\":\"submitted\"}\n"
    };
    for (unsigned test = 0; test < 2; ++test) {
        memset(s, 0, sizeof(*s));
        s->direction = test ? GREEN2_NAN_TCP_DOWNLOAD : GREEN2_NAN_TCP_UPLOAD;
        s->input_size = strlen(commands[test]);
        memcpy(s->input, commands[test], s->input_size);
        if (!test) {
            memcpy(s->input + s->input_size, header, 4);
            memset(s->input + s->input_size + 4, 0xa5, PAYLOAD_BYTES);
            s->input_size += 4 + PAYLOAD_BYTES + 4;
        }
        struct green2_nan_tcp_stats stats = {0};
        int result = green2_nan_tcp_command(&io, &stats);
        size_t json_at = test ? 6 + 4 + PAYLOAD_BYTES + 4 : 6;
        if (result != s->direction || stats.bytes != PAYLOAD_BYTES || stats.elapsed_ms != 1250 ||
            s->output_size != json_at + strlen(expected[test]) ||
            memcmp(s->output, "READY\n", 6) ||
            strcmp((char *)s->output + json_at, expected[test])) goto fail;
        if (test) {
            if (memcmp(s->output + 6, header, 4) ||
                memcmp(s->output + 10 + PAYLOAD_BYTES, terminator, 4)) goto fail;
            for (size_t i = 0; i < PAYLOAD_BYTES; ++i)
                if (s->output[10 + i] != (uint8_t)i) goto fail;
        }
        printf("[NAN] JSON self-test: %s", s->output + json_at);
    }
    vPortFree(s);
    puts("[NAN] 64-bit + upload/download JSON self-test PASS (synthetic I/O)");
    return 0;
fail:
    vPortFree(s);
    puts("[NAN] throughput JSON self-test FAIL");
    return -1;
}
