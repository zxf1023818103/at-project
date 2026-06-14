/*
 * Minimal compile-only <sys/socket.h> shim for picotls on bare-metal newlib (no POSIX
 * sockets). picotls's QUIC handshake path does not use sockets; the sockaddr types are only
 * referenced by peripheral ESNI/log helpers. Just enough types to compile lib/picotls.c.
 * PRIVATE to the picotls build (component/picotls_config) — never on a consumer's path.
 */
#ifndef AT_PICOTLS_COMPAT_SYS_SOCKET_H
#define AT_PICOTLS_COMPAT_SYS_SOCKET_H

#include <stdint.h>
#include <sys/types.h>

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

#define AF_INET    2
#define AF_INET6   10

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[ 14 ];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad[ 126 ];
};

#endif /* AT_PICOTLS_COMPAT_SYS_SOCKET_H */
