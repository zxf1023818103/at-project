#ifndef GREEN2_NAN_TCP_PROTOCOL_H
#define GREEN2_NAN_TCP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Bounded stream I/O. Return bytes transferred, or <= 0 on EOF/error/timeout.
 * Each call must finish within timeout_ms. now_ms is a wrapping monotonic clock.
 * No socket, allocator or operating-system dependency in the protocol engine. */
struct green2_nan_tcp_io {
    void *context;
    int (*read)(void *context, void *data, size_t size, uint32_t timeout_ms);
    int (*write)(void *context, const void *data, size_t size, uint32_t timeout_ms);
    uint32_t (*now_ms)(void *context);
};

enum green2_nan_tcp_result {
    GREEN2_NAN_TCP_ERROR = -1,
    GREEN2_NAN_TCP_CLOSED = 0,
    GREEN2_NAN_TCP_PING,
    GREEN2_NAN_TCP_UPLOAD,
    GREEN2_NAN_TCP_DOWNLOAD
};

struct green2_nan_tcp_stats {
    uint64_t bytes;
    uint32_t elapsed_ms;
};

/* Handle one command. Throughput leaves the stream open for the next direction;
 * PING, CLOSED and ERROR end the connection. Stats are valid on throughput success. */
enum green2_nan_tcp_result green2_nan_tcp_command(
    const struct green2_nan_tcp_io *io, struct green2_nan_tcp_stats *stats);

#endif
