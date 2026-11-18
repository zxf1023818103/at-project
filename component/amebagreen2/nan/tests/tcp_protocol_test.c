#include "nan_tcp_protocol.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct stream {
    uint8_t input[400000], output[4000000];
    size_t input_size, read_at, output_size;
    size_t read_chunk, write_chunk, fail_write_at, slow_read_at;
    uint32_t now, read_delay, write_delay;
};

static struct stream *fresh(void)
{
    struct stream *s = calloc(1, sizeof(*s));
    assert(s);
    s->read_chunk = 73;
    s->write_chunk = 31;
    s->fail_write_at = sizeof(s->output);
    s->slow_read_at = sizeof(s->input);
    s->read_delay = s->write_delay = 1;
    return s;
}

static int delay(struct stream *s, uint32_t milliseconds, uint32_t timeout)
{
    assert(timeout > 0 && timeout <= 10000);
    if (milliseconds > timeout) {
        s->now += timeout;
        return -1;
    }
    s->now += milliseconds;
    return 0;
}

static int receive(void *context, void *data, size_t size, uint32_t timeout)
{
    struct stream *s = context;
    /* Exercise a peer that makes progress but exceeds the overall deadline. */
    uint32_t wait = s->read_at >= s->slow_read_at ? 100 : s->read_delay;
    if (delay(s, wait, timeout)) return -1;
    if (size > s->read_chunk) size = s->read_chunk;
    if (size > s->input_size - s->read_at) size = s->input_size - s->read_at;
    memcpy(data, s->input + s->read_at, size);
    s->read_at += size;
    return (int)size;
}

static int transmit(void *context, const void *data, size_t size, uint32_t timeout)
{
    struct stream *s = context;
    if (delay(s, s->write_delay, timeout)) return -1;
    if (s->output_size >= s->fail_write_at) return -1;
    if (size > s->write_chunk) size = s->write_chunk;
    if (size > s->fail_write_at - s->output_size) size = s->fail_write_at - s->output_size;
    assert(s->output_size + size < sizeof(s->output));
    memcpy(s->output + s->output_size, data, size);
    s->output_size += size;
    s->output[s->output_size] = 0;
    return (int)size;
}

static uint32_t now(void *context) { return ((struct stream *)context)->now; }

static int command(struct stream *s, struct green2_nan_tcp_stats *stats)
{
    const struct green2_nan_tcp_io io = {s, receive, transmit, now};
    return green2_nan_tcp_command(&io, stats);
}

static void append(struct stream *s, const void *bytes, size_t size)
{
    assert(s->input_size + size <= sizeof(s->input));
    memcpy(s->input + s->input_size, bytes, size);
    s->input_size += size;
}
static void line(struct stream *s, const char *text) { append(s, text, strlen(text)); }
static void frame(struct stream *s, uint32_t size)
{
    uint8_t header[] = {(uint8_t)(size >> 24), (uint8_t)(size >> 16),
                        (uint8_t)(size >> 8), (uint8_t)size};
    append(s, header, sizeof(header));
    assert(s->input_size + size <= sizeof(s->input));
    memset(s->input + s->input_size, 0xa5, size);
    s->input_size += size;
}

static void check_json(const char *json, const char *id, const char *direction,
                       const char *counter, const struct green2_nan_tcp_stats *stats)
{
    char field[128];
    assert(strstr(json, "\"version\":1,"));
    snprintf(field, sizeof(field), "\"test_id\":\"%s\"", id);
    assert(strstr(json, field));
    snprintf(field, sizeof(field), "\"direction\":\"%s\"", direction);
    assert(strstr(json, field));
    snprintf(field, sizeof(field), "\"counter\":\"%s\"", counter);
    assert(strstr(json, field));
    uint64_t bytes = 0;
    double seconds = 0, mbps = -1;
    const char *values = strstr(json, "\"bytes\":");
    assert(values && sscanf(values, "\"bytes\":%" SCNu64 ",\"seconds\":%lf,\"mbps\":%lf",
                            &bytes, &seconds, &mbps) == 3);
    assert(bytes == stats->bytes && seconds > 0 && mbps >= 0);
    assert(seconds * 1000 >= stats->elapsed_ms - 0.01 &&
           seconds * 1000 <= stats->elapsed_ms + 0.01);
    double rate = bytes * 8.0 / seconds / 1000000;
    assert(mbps <= rate + 0.000001 && rate - mbps < 0.001001);
    assert(json[strlen(json) - 1] == '\n');
}

static void test_stream_and_clock_wrap(void)
{
    struct stream *s = fresh();
    s->now = UINT32_MAX - 100;
    line(s, "NAN-THROUGHPUT/1 Test-123 UPLOAD 1\n");
    frame(s, 65536);
    frame(s, 262144); /* Largest legal frame, split over thousands of reads. */
    frame(s, 33);
    frame(s, 0);
    line(s, "NAN-THROUGHPUT/1 Test-123 DOWNLOAD 1\n");
    line(s, "NAN-PING\n");
    struct green2_nan_tcp_stats stats;
    assert(command(s, &stats) == GREEN2_NAN_TCP_UPLOAD);
    assert(stats.bytes == 327713);
    assert(!memcmp(s->output, "READY\n", 6));
    check_json((char *)s->output + 6, "Test-123", "upload", "received", &stats);
    size_t offset = s->output_size;
    assert(command(s, &stats) == GREEN2_NAN_TCP_DOWNLOAD);
    assert(!memcmp(s->output + offset, "READY\n", 6));
    offset += 6;
    uint64_t received = 0;
    for (;;) {
        assert(offset + 4 < s->output_size);
        const uint8_t *p = s->output + offset;
        uint32_t length = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                          ((uint32_t)p[2] << 8) | p[3];
        offset += 4;
        if (!length) break;
        assert(length == 65536 && offset + length < s->output_size);
        for (uint32_t i = 0; i < length; ++i) assert(s->output[offset + i] == (uint8_t)i);
        offset += length;
        received += length;
    }
    assert(received > 0 && received == stats.bytes);
    check_json((char *)s->output + offset, "Test-123", "download", "submitted", &stats);
    offset = s->output_size;
    s->read_chunk = s->write_chunk = 1;
    assert(command(s, &stats) == GREEN2_NAN_TCP_PING);
    assert(s->output_size - offset == 9 && !memcmp(s->output + offset, "NAN-PONG\n", 9));
    assert(command(s, &stats) == GREEN2_NAN_TCP_CLOSED);
    free(s);
}

static void test_invalid_commands(void)
{
    const char *bad[] = {
        "NAN-THROUGHPUT/2 id UPLOAD 1\n", "NAN-THROUGHPUT/1 id UPLOAD 0\n",
        "NAN-THROUGHPUT/1 id DOWNLOAD 61\n", "NAN-THROUGHPUT/1 id UPLOAD -1\n",
        "NAN-THROUGHPUT/1 id UPLOAD 42949672961\n", "NAN-THROUGHPUT/1 id SIDEWAYS 1\n",
        "NAN-THROUGHPUT/1 quote\" UPLOAD 1\n", "NAN-THROUGHPUT/1 id UPLOAD 1 extra\n",
        "NAN-THROUGHPUT/1 id\n", "NAN-PING extra\n",
        "NAN-THROUGHPUT/1 01234567890123456789012345678901234567890123456789012345678901234 UPLOAD 1\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        struct stream *s = fresh();
        struct green2_nan_tcp_stats stats;
        line(s, bad[i]);
        assert(command(s, &stats) == GREEN2_NAN_TCP_ERROR && !s->output_size);
        free(s);
    }
    struct stream *s = fresh();
    struct green2_nan_tcp_stats stats;
    const char nul[] = "NAN-PING\0hidden\n";
    append(s, nul, sizeof(nul) - 1);
    assert(command(s, &stats) == GREEN2_NAN_TCP_ERROR && !s->output_size);
    s->input_size = s->read_at = 0;
    memset(s->input, 'A', 256);
    s->input_size = 256;
    assert(command(s, &stats) == GREEN2_NAN_TCP_ERROR && !s->output_size);
    free(s);
}

static void test_broken_streams(void)
{
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        struct stream *s = fresh();
        struct green2_nan_tcp_stats stats = {0};
        line(s, scenario == 4 ? "NAN-THROUGHPUT/1 id DOWNLOAD 1\n" :
                               "NAN-THROUGHPUT/1 id UPLOAD 1\n");
        if (scenario == 0) { /* Oversize frame: reject before consuming payload. */
            const uint8_t header[] = {0, 4, 0, 1};
            append(s, header, 4);
        } else if (scenario == 1) { /* Truncated payload must not yield a result. */
            frame(s, 1000);
            s->input_size -= 901;
        } else if (scenario == 2) { /* Slow command with individual bytes arriving. */
            s->read_delay = 1000;
        } else if (scenario == 3) { /* Progress cannot extend the phase indefinitely. */
            s->slow_read_at = s->input_size;
            frame(s, 262144);
            frame(s, 0);
        } else { /* Partial TX then cancellation/disconnection. */
            s->fail_write_at = 1000;
        }
        assert(command(s, &stats) == GREEN2_NAN_TCP_ERROR);
        assert(stats.bytes == 0);
        assert(!strstr((char *)s->output, "\"version\""));
        if (scenario == 2) assert(s->now <= 10000);
        if (scenario == 3) assert(s->now < 32000 && s->read_at < s->input_size);
        free(s);
    }
}

static void test_max_command_and_empty_upload(void)
{
    struct stream *s = fresh();
    struct green2_nan_tcp_stats stats;
    const char *id = "0123456789012345678901234567890123456789012345678901234567890123";
    char text[128];
    snprintf(text, sizeof(text), "NAN-THROUGHPUT/1 %s UPLOAD 60\n", id);
    line(s, text);
    frame(s, 0);
    s->read_delay = s->write_delay = 0;
    assert(command(s, &stats) == GREEN2_NAN_TCP_UPLOAD && stats.bytes == 0);
    check_json((char *)s->output + 6, id, "upload", "received", &stats);
    free(s);
}

int main(void)
{
    test_stream_and_clock_wrap();
    test_invalid_commands();
    test_broken_streams();
    test_max_command_and_empty_upload();
    puts("NAN TCP: fragmented bidirectional streams, byte counts, bounds, deadlines and clock wrap PASS");
    return 0;
}
