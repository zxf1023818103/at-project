/*
 * Minimal compile-only <netinet/in.h> shim for picotls on bare-metal newlib. See the note in
 * the sibling sys/socket.h shim. Little-endian (Cortex-M) byte-order helpers included here so
 * the partner arpa/inet.h shim only adds the inet_pton/ntop decls (no double definition).
 */
#ifndef AT_PICOTLS_COMPAT_NETINET_IN_H
#define AT_PICOTLS_COMPAT_NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

typedef uint16_t in_port_t;

struct in_addr  { uint32_t s_addr; };
struct in6_addr { uint8_t  s6_addr[ 16 ]; };

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    uint8_t        sin_zero[ 8 ];
};

struct sockaddr_in6 {
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN  16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

static inline uint16_t htons( uint16_t x ) { return ( uint16_t ) ( ( x << 8 ) | ( x >> 8 ) ); }
static inline uint16_t ntohs( uint16_t x ) { return htons( x ); }
static inline uint32_t htonl( uint32_t x )
{
    return ( ( x & 0xFFu ) << 24 ) | ( ( x & 0xFF00u ) << 8 ) |
           ( ( x >> 8 ) & 0xFF00u ) | ( ( x >> 24 ) & 0xFFu );
}
static inline uint32_t ntohl( uint32_t x ) { return htonl( x ); }

#endif /* AT_PICOTLS_COMPAT_NETINET_IN_H */
