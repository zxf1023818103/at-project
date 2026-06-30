/*
 * <netinet/in.h> — IPv4/IPv6 address types + IPPROTO_* for the FreeRTOS-Plus-TCP
 * BSD-sockets shim (component/syscall_config). IPv6 types are defined so iperf3's
 * dual-stack code compiles; the shim itself is IPv4-functional (v6 paths return errors).
 */
#ifndef _AT_NETINET_IN_H
#define _AT_NETINET_IN_H

#include <sys/socket.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

/* Protocols (setsockopt levels too) */
#define IPPROTO_IP    0
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17
#define IPPROTO_IPV6  41

/* IP-level setsockopt names (accepted-and-ignored by the shim) */
#define IP_TOS  1

#define INADDR_ANY        ((in_addr_t)0x00000000)
#define INADDR_NONE       ((in_addr_t)0xffffffff)
#define INADDR_LOOPBACK   ((in_addr_t)0x7f000001)
#define INET_ADDRSTRLEN   16
#define INET6_ADDRSTRLEN  46

struct in_addr  { in_addr_t s_addr; };
struct in6_addr { uint8_t s6_addr[16]; };

struct sockaddr_in {
    sa_family_t    sin_family;   /* AF_INET */
    in_port_t      sin_port;     /* network byte order */
    struct in_addr sin_addr;
    char           sin_zero[8];
};

struct sockaddr_in6 {
    sa_family_t     sin6_family; /* AF_INET6 */
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

extern const struct in6_addr in6addr_any;

#define IN6_IS_ADDR_LINKLOCAL(a) \
    (((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))

#ifdef __cplusplus
}
#endif

#endif /* _AT_NETINET_IN_H */
