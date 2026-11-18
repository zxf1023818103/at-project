#include "nan_tcp_protocol.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define COMMAND_LIMIT 256
#define IO_TIMEOUT_MS 10000u
#define BLOCK_BYTES 65536u
#define MAX_FRAME_BYTES 262144u
#define MAX_SECONDS 60u

struct deadline {
    uint32_t started;
    uint32_t limit;
};

static uint32_t remaining(const struct green2_nan_tcp_io *io, struct deadline end)
{
    uint32_t elapsed = io->now_ms(io->context) - end.started;
    if (elapsed >= end.limit) return 0;
    uint32_t left = end.limit - elapsed;
    return left < IO_TIMEOUT_MS ? left : IO_TIMEOUT_MS;
}

static int read_exact(const struct green2_nan_tcp_io *io, void *data, size_t size,
                      struct deadline end)
{
    uint8_t *p = data;
    while (size) {
        uint32_t timeout = remaining(io, end);
        if (!timeout) return -1;
        int n = io->read(io->context, p, size, timeout);
        if (n <= 0 || (size_t)n > size) return -1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int write_all(const struct green2_nan_tcp_io *io, const void *data, size_t size,
                     struct deadline end)
{
    const uint8_t *p = data;
    while (size) {
        uint32_t timeout = remaining(io, end);
        if (!timeout) return -1;
        int n = io->write(io->context, p, size, timeout);
        if (n <= 0 || (size_t)n > size) return -1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int read_line(const struct green2_nan_tcp_io *io, char line[COMMAND_LIMIT],
                     struct deadline end)
{
    for (size_t used = 0; used < COMMAND_LIMIT - 1; ++used) {
        uint32_t timeout = remaining(io, end);
        if (!timeout) return GREEN2_NAN_TCP_ERROR;
        unsigned char c;
        if (io->read(io->context, &c, 1, timeout) != 1)
            return used ? GREEN2_NAN_TCP_ERROR : GREEN2_NAN_TCP_CLOSED;
        if (c == '\n') {
            line[used] = 0;
            return 1;
        }
        /* Reject embedded NULs and non-ASCII instead of truncating the command. */
        if ((c < 32 && c != '\t' && c != '\r') || c > 126)
            return GREEN2_NAN_TCP_ERROR;
        line[used] = (char)c;
    }
    return GREEN2_NAN_TCP_ERROR;
}

static int parse_command(char *line, char **id, unsigned *seconds)
{
    char *fields[4];
    unsigned count = 0;
    char *p = line;
    while (*p) {
        p += strspn(p, " \t\r");
        if (!*p) break;
        if (count == 4) return GREEN2_NAN_TCP_ERROR;
        fields[count++] = p;
        p += strcspn(p, " \t\r");
        if (*p) *p++ = 0;
    }
    if (count != 4 || strcmp(fields[0], "NAN-THROUGHPUT/1"))
        return GREEN2_NAN_TCP_ERROR;
    size_t len = strlen(fields[1]);
    if (!len || len > 64) return GREEN2_NAN_TCP_ERROR;
    for (size_t i = 0; i < len; ++i) {
        char c = fields[1][i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '-')) return GREEN2_NAN_TCP_ERROR;
    }
    int direction;
    if (!strcmp(fields[2], "UPLOAD")) direction = GREEN2_NAN_TCP_UPLOAD;
    else if (!strcmp(fields[2], "DOWNLOAD")) direction = GREEN2_NAN_TCP_DOWNLOAD;
    else return GREEN2_NAN_TCP_ERROR;
    unsigned duration = 0;
    for (p = fields[3]; *p; ++p) {
        if (*p < '0' || *p > '9') return GREEN2_NAN_TCP_ERROR;
        duration = duration * 10 + (unsigned)(*p - '0');
        if (duration > MAX_SECONDS) return GREEN2_NAN_TCP_ERROR;
    }
    if (!duration) return GREEN2_NAN_TCP_ERROR;
    *seconds = duration;
    *id = fields[1];
    return direction;
}

enum green2_nan_tcp_result green2_nan_tcp_command(
    const struct green2_nan_tcp_io *io, struct green2_nan_tcp_stats *stats)
{
    char command[COMMAND_LIMIT];
    struct deadline end = {io->now_ms(io->context), IO_TIMEOUT_MS};
    int rc = read_line(io, command, end);
    if (rc <= 0) return rc;
    if (!strcmp(command, "NAN-PING"))
        return write_all(io, "NAN-PONG\n", 9, end) ? GREEN2_NAN_TCP_ERROR : GREEN2_NAN_TCP_PING;

    char *id;
    unsigned seconds;
    int direction = parse_command(command, &id, &seconds);
    if (direction < 0) return GREEN2_NAN_TCP_ERROR;
    end.started = io->now_ms(io->context);
    end.limit = (seconds + 30u) * 1000u;
    if (write_all(io, "READY\n", 6, end)) return GREEN2_NAN_TCP_ERROR;
    uint32_t started = io->now_ms(io->context);
    uint64_t count = 0;
    /* Feed several TCP segments per call without allocating a whole frame. */
    uint8_t buffer[4096];
    if (direction == GREEN2_NAN_TCP_UPLOAD) {
        for (;;) {
            uint8_t header[4];
            if (read_exact(io, header, sizeof(header), end)) return GREEN2_NAN_TCP_ERROR;
            uint32_t size = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
                            ((uint32_t)header[2] << 8) | header[3];
            if (!size) break;
            if (size > MAX_FRAME_BYTES) return GREEN2_NAN_TCP_ERROR;
            uint32_t left = size;
            while (left) {
                size_t chunk = left < sizeof(buffer) ? left : sizeof(buffer);
                if (read_exact(io, buffer, chunk, end)) return GREEN2_NAN_TCP_ERROR;
                left -= (uint32_t)chunk;
            }
            count += size;
        }
    } else {
        for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = (uint8_t)i;
        static const uint8_t header[4] = {0, 1, 0, 0};
        do {
            if (write_all(io, header, sizeof(header), end)) return GREEN2_NAN_TCP_ERROR;
            for (unsigned offset = 0; offset < BLOCK_BYTES; offset += sizeof(buffer))
                if (write_all(io, buffer, sizeof(buffer), end)) return GREEN2_NAN_TCP_ERROR;
            count += BLOCK_BYTES;
        } while ((uint32_t)(io->now_ms(io->context) - started) < seconds * 1000u);
        static const uint8_t terminator[4];
        if (write_all(io, terminator, sizeof(terminator), end)) return GREEN2_NAN_TCP_ERROR;
    }
    uint32_t elapsed = io->now_ms(io->context) - started;
    if (!elapsed) elapsed = 1; /* A zero-byte local test must still emit finite JSON. */
    uint64_t kbps = count * 8u / elapsed;
    char result[320];
    int length = snprintf(result, sizeof(result),
        "{\"version\":1,\"test_id\":\"%s\",\"direction\":\"%s\",\"bytes\":%" PRIu64
        ",\"seconds\":%lu.%03lu,\"mbps\":%" PRIu64 ".%03" PRIu64 ",\"counter\":\"%s\"}\n",
        id, direction == GREEN2_NAN_TCP_UPLOAD ? "upload" : "download", count,
        (unsigned long)(elapsed / 1000u), (unsigned long)(elapsed % 1000u),
        kbps / 1000u, kbps % 1000u,
        direction == GREEN2_NAN_TCP_UPLOAD ? "received" : "submitted");
    if (length < 0 || (size_t)length >= sizeof(result) ||
        write_all(io, result, (size_t)length, end)) return GREEN2_NAN_TCP_ERROR;
    stats->bytes = count;
    stats->elapsed_ms = elapsed;
    return direction;
}
