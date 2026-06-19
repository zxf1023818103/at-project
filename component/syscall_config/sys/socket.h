/*
 * <sys/socket.h> — minimal BSD-sockets surface for porting POSIX network apps
 * (iperf3) onto FreeRTOS-Plus-TCP. newlib ships no <sys/socket.h>; this provides the
 * standard types/constants/prototypes, and component/syscall_config/bsd_socket.c maps
 * them onto the FreeRTOS_* API. Place this dir on the include path ahead of the sysroot.
 *
 * Feature-macro note: many apps gate optional socket options with `#ifdef SO_xxx` /
 * `#ifdef TCP_xxx`. We deliberately define ONLY the options the FreeRTOS-Plus-TCP
 * backend actually supports, so the unsupported branches compile out.
 */
#ifndef _AT_SYS_SOCKET_H
#define _AT_SYS_SOCKET_H

#include <sys/types.h>
#include <sys/select.h>   /* newlib: fd_set, FD_ZERO/SET/CLR/ISSET, select() proto */
#include <sys/uio.h>      /* struct iovec (newlib provides this) */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __socklen_t_defined
typedef uint32_t socklen_t;
#define __socklen_t_defined 1
#endif
typedef uint16_t sa_family_t;

/* Address families */
#define AF_UNSPEC   0
#define AF_INET     2
#define AF_INET6    10
#define PF_UNSPEC   AF_UNSPEC
#define PF_INET     AF_INET
#define PF_INET6    AF_INET6

/* Socket types */
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

/* setsockopt levels */
#define SOL_SOCKET  0xfff

/* SOL_SOCKET options actually backed by the FreeRTOS-Plus-TCP shim */
#define SO_REUSEADDR 0x0004
#define SO_SNDBUF    0x1001
#define SO_RCVBUF    0x1002
#define SO_SNDTIMEO  0x1005
#define SO_RCVTIMEO  0x1006
#define SO_ERROR     0x1007

/* shutdown() how */
#define SHUT_RD     0
#define SHUT_WR     1
#define SHUT_RDWR   2

/* send/recv flags (only DONTWAIT is honored; others are no-ops in the shim) */
#define MSG_DONTWAIT 0x40
#define MSG_PEEK     0x02
#define MSG_TRUNC    0x20

/* Generic socket address. 16-byte sa_data covers sockaddr_in; callers cast to
 * struct sockaddr_in / sockaddr_in6 / sockaddr_storage. */
struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

#define _SS_MAXSIZE   128
struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad[_SS_MAXSIZE - sizeof(sa_family_t)];
};

/* msghdr / cmsghdr — declared so net.c's sendmsg/recvmsg paths compile; the UDP
 * GSO/GRO code that uses cmsg is gated off (UDP_SEGMENT/UDP_GRO undefined). */
struct msghdr {
    void         *msg_name;
    socklen_t     msg_namelen;
    struct iovec *msg_iov;
    int           msg_iovlen;
    void         *msg_control;
    socklen_t     msg_controllen;
    int           msg_flags;
};
struct cmsghdr {
    socklen_t cmsg_len;
    int       cmsg_level;
    int       cmsg_type;
};

int     socket(int domain, int type, int protocol);
int     bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int     listen(int fd, int backlog);
int     accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
int     connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t recv(int fd, void *buf, size_t len, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dst, socklen_t dstlen);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *src, socklen_t *srclen);
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int fd, struct msghdr *msg, int flags);
int     setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen);
int     getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen);
int     shutdown(int fd, int how);
int     getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif /* _AT_SYS_SOCKET_H */
