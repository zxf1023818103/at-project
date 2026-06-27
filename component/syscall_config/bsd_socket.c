/*
 * bsd_socket.c — a BSD-sockets / POSIX-net layer over FreeRTOS-Plus-TCP, for porting
 * POSIX network apps (iperf3) that call socket()/bind()/connect()/select()/getaddrinfo()
 * rather than the native FreeRTOS_* API. Part of component/syscall_config (the newlib
 * retarget), so it ships in the image whenever a consumer references these symbols and
 * is dead-stripped otherwise.
 *
 * Model: socket() creates a FreeRTOS socket and registers it as a POSIX fd via
 * fd_open_socket() (so read()/write()/close() already work through the fd VFS). The ops
 * that need the native handle map fd -> Socket_t via fd_get_socket(). IPv4 only; the
 * IPv6/exotic-sockopt paths return errors or are accepted-and-ignored.
 *
 * select() bridges onto FreeRTOS_select (needs ipconfigSUPPORT_SELECT_FUNCTION=1).
 */
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"
#include "syscalls.h"

/* FreeRTOS-Plus-TCP's IPv4-backward-compat header does `#define sin_addr
 * sin_address.ulIP_IPv4`, which would clobber the POSIX struct sockaddr_in.sin_addr
 * member below. We access the FreeRTOS field as sin_address.ulIP_IPv4 explicitly, so
 * drop the macro before the POSIX net headers. */
#undef sin_addr

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <poll.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <termios.h>

/* ------------------------------------------------------------------ helpers --- */

const struct in6_addr in6addr_any = { { 0 } };   /* iperf references this in its v6 paths */

static Socket_t sock(int fd)
{
    return (Socket_t)fd_get_socket(fd);
}

/* POSIX sockaddr_in (network-order port + addr) -> FreeRTOS freertos_sockaddr. */
static int to_freertos(const struct sockaddr *sa, socklen_t len, struct freertos_sockaddr *out)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)sa;

    if (sa == NULL || len < (socklen_t)sizeof(*in) || in->sin_family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->sin_len = (uint8_t)sizeof(*out);
    out->sin_family = FREERTOS_AF_INET;
    out->sin_port = in->sin_port;                       /* both network order */
    out->sin_address.ulIP_IPv4 = in->sin_addr.s_addr;   /* both network order */
    return 0;
}

static void from_freertos(const struct freertos_sockaddr *fa, struct sockaddr *sa, socklen_t *len)
{
    struct sockaddr_in in;

    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = fa->sin_port;
    in.sin_addr.s_addr = fa->sin_address.ulIP_IPv4;
    if (sa != NULL && len != NULL) {
        socklen_t n = (*len < (socklen_t)sizeof(in)) ? *len : (socklen_t)sizeof(in);
        memcpy(sa, &in, n);
        *len = (socklen_t)sizeof(in);
    }
}

/* ---- connected-UDP shim (for c-ares): FreeRTOS+TCP has no connect() on a UDP socket, so we
 * remember the peer set via connect() on a SOCK_DGRAM fd and route send()/recv() on it through
 * FreeRTOS_sendto()/FreeRTOS_recvfrom(). socket() (re)registers a fd, so closed-fd reuse is fine. */
static struct udp_ent {
    int                      fd;        /* 0 = free (socket fds are >= 3) */
    int                      has_peer;
    struct freertos_sockaddr peer;
} s_udp[ 8 ];

static struct udp_ent *udp_find(int fd)
{
    int i;
    for (i = 0; i < 8; i++) { if (s_udp[i].fd == fd) { return &s_udp[i]; } }
    return NULL;
}

static void udp_track(int fd, int is_udp)
{
    struct udp_ent *e = udp_find(fd);
    if (is_udp) {
        if (e == NULL) { int i; for (i = 0; i < 8; i++) { if (s_udp[i].fd == 0) { e = &s_udp[i]; break; } } }
        if (e != NULL) { e->fd = fd; e->has_peer = 0; }
    } else if (e != NULL) {
        e->fd = 0; e->has_peer = 0;
    }
}

/* ------------------------------------------------------------ socket lifecycle --- */

int socket(int domain, int type, int protocol)
{
    BaseType_t ftype, fproto;
    Socket_t s;
    int fd;
    (void)protocol;

    if (domain != AF_INET) {            /* IPv4 only */
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (type == SOCK_STREAM) { ftype = FREERTOS_SOCK_STREAM; fproto = FREERTOS_IPPROTO_TCP; }
    else if (type == SOCK_DGRAM) { ftype = FREERTOS_SOCK_DGRAM; fproto = FREERTOS_IPPROTO_UDP; }
    else { errno = EINVAL; return -1; }

    s = FreeRTOS_socket(FREERTOS_AF_INET, ftype, fproto);
    if (s == FREERTOS_INVALID_SOCKET || s == NULL) {
        errno = ENOMEM;
        return -1;
    }
    fd = fd_open_socket((void *)s);
    if (fd < 0) {
        FreeRTOS_closesocket(s);
        return -1;
    }
    udp_track(fd, type == SOCK_DGRAM);
    return fd;
}

int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    Socket_t s = sock(fd);
    struct freertos_sockaddr fa;

    if (s == NULL) { errno = EBADF; return -1; }
    if (to_freertos(addr, addrlen, &fa) != 0) { return -1; }
    if (FreeRTOS_bind(s, &fa, sizeof(fa)) != 0) { errno = EADDRINUSE; return -1; }
    return 0;
}

int listen(int fd, int backlog)
{
    Socket_t s = sock(fd);

    if (s == NULL) { errno = EBADF; return -1; }
    if (backlog > 0x7fff) { backlog = 0x7fff; }
    if (FreeRTOS_listen(s, (BaseType_t)backlog) != 0) { errno = EOPNOTSUPP; return -1; }
    return 0;
}

int accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    Socket_t s = sock(fd), c;
    struct freertos_sockaddr fa;
    socklen_t falen = sizeof(fa);
    int cfd;

    if (s == NULL) { errno = EBADF; return -1; }
    c = FreeRTOS_accept(s, &fa, &falen);
    if (c == FREERTOS_INVALID_SOCKET || c == NULL) { errno = EAGAIN; return -1; }
    if (addr != NULL && addrlen != NULL) { from_freertos(&fa, addr, addrlen); }
    cfd = fd_open_socket((void *)c);
    if (cfd < 0) { FreeRTOS_closesocket(c); return -1; }
    return cfd;
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    Socket_t s = sock(fd);
    struct freertos_sockaddr fa;
    struct udp_ent *e;

    if (s == NULL) { errno = EBADF; return -1; }
    if (to_freertos(addr, addrlen, &fa) != 0) { return -1; }
    e = udp_find(fd);
    if (e != NULL) {                       /* connected UDP: remember the peer, no FreeRTOS_connect */
        e->peer = fa;
        e->has_peer = 1;
        return 0;
    }
    {
        BaseType_t r = FreeRTOS_connect(s, &fa, sizeof(fa));
        if (r == 0) { return 0; }
        /* A non-blocking socket (c-ares sets SO_RCVTIMEO/SNDTIMEO=0) reports the connect as
         * in-progress; surface EINPROGRESS so the caller waits for writable via select() instead
         * of treating it as a hard failure. A blocking socket (e.g. iperf) returns 0 above. */
        if (r == -pdFREERTOS_ERRNO_EWOULDBLOCK || r == -pdFREERTOS_ERRNO_EINPROGRESS) {
            errno = EINPROGRESS; return -1;
        }
        errno = ECONNREFUSED; return -1;
    }
}

int shutdown(int fd, int how)
{
    Socket_t s = sock(fd);
    (void)how;
    if (s == NULL) { errno = EBADF; return -1; }
    (void)FreeRTOS_shutdown(s, FREERTOS_SHUT_RDWR);
    return 0;
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    Socket_t s = sock(fd);
    struct freertos_sockaddr fa;

    if (s == NULL) { errno = EBADF; return -1; }
    memset(&fa, 0, sizeof(fa));
    (void)FreeRTOS_GetLocalAddress(s, &fa);
    from_freertos(&fa, addr, addrlen);
    return 0;
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    Socket_t s = sock(fd);
    struct freertos_sockaddr fa;

    if (s == NULL) { errno = EBADF; return -1; }
    memset(&fa, 0, sizeof(fa));
    (void)FreeRTOS_GetRemoteAddress(s, &fa);
    from_freertos(&fa, addr, addrlen);
    return 0;
}

/* --------------------------------------------------------------------- I/O --- */

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
    Socket_t s = sock(fd);
    BaseType_t f = (flags & MSG_DONTWAIT) ? FREERTOS_MSG_DONTWAIT : 0;
    struct udp_ent *e;
    BaseType_t r;

    if (s == NULL) { errno = EBADF; return -1; }
    e = udp_find(fd);
    if (e != NULL && e->has_peer) {        /* connected UDP -> recvfrom (source ignored) */
        struct freertos_sockaddr fa;
        socklen_t falen = sizeof(fa);
        r = FreeRTOS_recvfrom(s, buf, len, f, &fa, &falen);
    } else {
        r = FreeRTOS_recv(s, buf, len, f);
    }
    if (r < 0) { errno = (r == -pdFREERTOS_ERRNO_EWOULDBLOCK) ? EWOULDBLOCK : EIO; return -1; }
    return (ssize_t)r;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    Socket_t s = sock(fd);
    BaseType_t f = (flags & MSG_DONTWAIT) ? FREERTOS_MSG_DONTWAIT : 0;
    struct udp_ent *e;
    BaseType_t r;

    if (s == NULL) { errno = EBADF; return -1; }
    e = udp_find(fd);
    if (e != NULL && e->has_peer) {        /* connected UDP -> sendto the remembered peer */
        r = FreeRTOS_sendto(s, buf, len, f, &e->peer, sizeof(e->peer));
    } else {
        r = FreeRTOS_send(s, buf, len, f);
    }
    if (r < 0) { errno = (r == -pdFREERTOS_ERRNO_EWOULDBLOCK) ? EWOULDBLOCK : EIO; return -1; }
    return (ssize_t)r;
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *srclen)
{
    Socket_t s = sock(fd);
    struct freertos_sockaddr fa;
    socklen_t falen = sizeof(fa);
    BaseType_t f = (flags & MSG_DONTWAIT) ? FREERTOS_MSG_DONTWAIT : 0;
    int32_t r;

    if (s == NULL) { errno = EBADF; return -1; }
    r = FreeRTOS_recvfrom(s, buf, len, f, &fa, &falen);
    if (r < 0) { errno = (r == -pdFREERTOS_ERRNO_EWOULDBLOCK) ? EWOULDBLOCK : EIO; return -1; }
    if (src != NULL && srclen != NULL) { from_freertos(&fa, src, srclen); }
    return (ssize_t)r;
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *dst, socklen_t dstlen)
{
    Socket_t s = sock(fd);
    struct freertos_sockaddr fa;
    BaseType_t f = (flags & MSG_DONTWAIT) ? FREERTOS_MSG_DONTWAIT : 0;
    int32_t r;

    if (s == NULL) { errno = EBADF; return -1; }
    if (to_freertos(dst, dstlen, &fa) != 0) { return -1; }
    r = FreeRTOS_sendto(s, buf, len, f, &fa, sizeof(fa));
    if (r < 0) { errno = EIO; return -1; }
    return (ssize_t)r;
}

/* ----------------------------------------------------------------- sockopt --- */

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    Socket_t s = sock(fd);

    if (s == NULL) { errno = EBADF; return -1; }
    /* Map the timeouts the shim can honor; accept-and-ignore everything else (Nagle,
     * buffer sizes, TOS, keepalive, ...) so the caller's option-setting succeeds. */
    if (level == SOL_SOCKET && (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
        optval != NULL && optlen >= (socklen_t)sizeof(struct timeval)) {
        const struct timeval *tv = (const struct timeval *)optval;
        TickType_t ticks = (TickType_t)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
        BaseType_t opt = (optname == SO_RCVTIMEO) ? FREERTOS_SO_RCVTIMEO : FREERTOS_SO_SNDTIMEO;
        (void)FreeRTOS_setsockopt(s, 0, opt, &ticks, sizeof(ticks));
    }
    return 0;
}

int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    Socket_t s = sock(fd);

    if (s == NULL) { errno = EBADF; return -1; }
    /* TCP_MAXSEG: report a typical Ethernet MSS so iperf doesn't warn "nonsense TCP MSS 0". */
    if (level == IPPROTO_TCP && optname == TCP_MAXSEG &&
        optval != NULL && optlen != NULL && *optlen >= (socklen_t)sizeof(int)) {
        *(int *)optval = 1460;
        *optlen = sizeof(int);
        return 0;
    }
    /* SO_ERROR is the only one iperf reads after a connect; report no error. */
    if (level == SOL_SOCKET && optname == SO_ERROR &&
        optval != NULL && optlen != NULL && *optlen >= (socklen_t)sizeof(int)) {
        *(int *)optval = 0;
        *optlen = sizeof(int);
        return 0;
    }
    if (optval != NULL && optlen != NULL && *optlen >= (socklen_t)sizeof(int)) {
        *(int *)optval = 0;
        *optlen = sizeof(int);
    }
    return 0;
}

/* --------------------------------------------------------- select() / poll() --- */
/* Bridge onto FreeRTOS_select (ipconfigSUPPORT_SELECT_FUNCTION=1). One socket set is
 * built per call from the input fd_sets; a fd that maps to no socket is skipped. */
/* One persistent socket set, reused across calls. Creating/deleting a set every select()
 * churns its event group and left sockets with a dangling pxSocketSet (the earlier
 * xEventGroupSetBits/heap faults). Only the iperf main thread calls select(), so a single
 * static set is safe; each call FD_SETs, selects, then FD_CLRs (unbinds) before returning. */
static SocketSet_t s_sel_set;

int select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *timeout)
{
    SocketSet_t set;
    TickType_t ticks;
    int fd, ready = 0;
    (void)efds;

    if (s_sel_set == NULL) {
        s_sel_set = FreeRTOS_CreateSocketSet();
        if (s_sel_set == NULL) { errno = ENOMEM; return -1; }
    }
    set = s_sel_set;

    for (fd = 0; fd < nfds; fd++) {
        Socket_t s = sock(fd);
        EventBits_t bits = 0;
        if (s == NULL) { continue; }
        if (rfds && FD_ISSET(fd, rfds)) { bits |= eSELECT_READ | eSELECT_EXCEPT; }
        if (wfds && FD_ISSET(fd, wfds)) { bits |= eSELECT_WRITE; }
        if (bits) { FreeRTOS_FD_SET(s, set, bits); }
    }

    if (timeout == NULL) {
        ticks = portMAX_DELAY;
    } else {
        ticks = (TickType_t)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    }
    (void)FreeRTOS_select(set, ticks);

    for (fd = 0; fd < nfds; fd++) {
        Socket_t s = sock(fd);
        int in_r = (rfds && FD_ISSET(fd, rfds));
        int in_w = (wfds && FD_ISSET(fd, wfds));
        EventBits_t got;
        if (s == NULL || (!in_r && !in_w)) { continue; }
        got = FreeRTOS_FD_ISSET(s, set);
        /* Unbind the socket from this set BEFORE deleting the set, else the socket keeps a
         * dangling pxSocketSet and the next event faults in xEventGroupSetBits (deadbeef). */
        FreeRTOS_FD_CLR(s, set, eSELECT_ALL);
        if (in_r) { if (got & (eSELECT_READ | eSELECT_EXCEPT)) { ready++; } else { FD_CLR(fd, rfds); } }
        if (in_w) { if (got & eSELECT_WRITE) { ready++; } else { FD_CLR(fd, wfds); } }
    }
    return ready;   /* set is persistent (s_sel_set); sockets were FD_CLR'd above */
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    fd_set rset, wset;
    struct timeval tv, *ptv = NULL;
    nfds_t i;
    int maxfd = 0, n;

    FD_ZERO(&rset);
    FD_ZERO(&wset);
    for (i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) { continue; }
        if (fds[i].events & POLLIN)  { FD_SET(fds[i].fd, &rset); }
        if (fds[i].events & POLLOUT) { FD_SET(fds[i].fd, &wset); }
        if (fds[i].fd > maxfd) { maxfd = fds[i].fd; }
        fds[i].revents = 0;
    }
    if (timeout >= 0) { tv.tv_sec = timeout / 1000; tv.tv_usec = (timeout % 1000) * 1000; ptv = &tv; }
    n = select(maxfd + 1, &rset, &wset, NULL, ptv);
    if (n < 0) { return -1; }
    n = 0;
    for (i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) { continue; }
        if ((fds[i].events & POLLIN)  && FD_ISSET(fds[i].fd, &rset)) { fds[i].revents |= POLLIN; }
        if ((fds[i].events & POLLOUT) && FD_ISSET(fds[i].fd, &wset)) { fds[i].revents |= POLLOUT; }
        if (fds[i].revents) { n++; }
    }
    return n;
}

/* --------------------------------------------------------- name resolution --- */

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo *ai;
    struct sockaddr_in *sa;
    in_addr_t ip = 0;

    if (node == NULL) {                                  /* AI_PASSIVE: bind any */
        ip = INADDR_ANY;
    } else if (inet_pton(AF_INET, node, &ip) != 1) {     /* numeric literal? */
        uint32_t h = FreeRTOS_gethostbyname(node);       /* else DNS (IPv4) */
        if (h == 0) { return EAI_NONAME; }
        ip = h;                                          /* network order */
    }

    ai = (struct addrinfo *)calloc(1, sizeof(*ai));
    sa = (struct sockaddr_in *)calloc(1, sizeof(*sa));
    if (ai == NULL || sa == NULL) { free(ai); free(sa); return EAI_MEMORY; }

    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = ip;
    sa->sin_port = htons((uint16_t)(service ? atoi(service) : 0));

    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen = sizeof(*sa);
    ai->ai_addr = (struct sockaddr *)sa;
    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res != NULL) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_addr);
        free(res);
        res = next;
    }
}

const char *gai_strerror(int errcode)
{
    switch (errcode) {
    case 0:           return "ok";
    case EAI_NONAME:  return "name resolution failed";
    case EAI_MEMORY:  return "out of memory";
    default:          return "address lookup error";
    }
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET) { errno = EAFNOSUPPORT; return NULL; }
    return FreeRTOS_inet_ntop(FREERTOS_AF_INET, src, dst, size);
}

unsigned int if_nametoindex(const char *ifname) { (void)ifname; return 0; }
char *if_indextoname(unsigned int i, char *name) { (void)i; (void)name; return NULL; }

/* --------------------------------------------------------- misc POSIX stubs --- */

int uname(struct utsname *u)
{
    if (u == NULL) { errno = EFAULT; return -1; }
    strcpy(u->sysname, "FreeRTOS");
    strcpy(u->nodename, "amebad");
    strcpy(u->release, tskKERNEL_VERSION_NUMBER);
    strcpy(u->version, "");
    strcpy(u->machine, "arm-cortex-m33");
    return 0;
}

/* iperf mmaps a temp-file-backed per-stream buffer; back it with the heap (the fd and
 * offset are ignored, so mkstemp/ftruncate just need to not fail). */
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *p;
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    p = malloc(length);
    return (p == NULL) ? MAP_FAILED : p;
}
int munmap(void *addr, size_t length) { (void)length; free(addr); return 0; }

int tcgetattr(int fd, struct termios *t) { (void)fd; if (t) memset(t, 0, sizeof(*t)); return 0; }
int tcsetattr(int fd, int act, const struct termios *t) { (void)fd; (void)act; (void)t; return 0; }

/* ftruncate: newlib ships none. iperf only ftruncates the mkstemp fd before mmap (-> malloc,
 * which ignores the fd), so a no-op is fine. signal/strsignal/mkstemp/getline are provided by
 * newlib's libc — do NOT redefine them (multiple-definition link errors). */
int ftruncate(int fd, off_t length) { (void)fd; (void)length; return 0; }
unsigned int sleep(unsigned int seconds) { vTaskDelay(pdMS_TO_TICKS(seconds * 1000U)); return 0; }

#include <sys/resource.h>
int getrusage(int who, struct rusage *usage) { (void)who; if (usage) memset(usage, 0, sizeof(*usage)); return 0; }

/* ------------------------------------------------------------- c-ares extras --- */
#include <netdb.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/uio.h>

/* c-ares' service-name->port path calls getservbyname(); we always pass a NULL service, so this
 * is never actually reached. Stub it so the symbol resolves. */
struct servent *getservbyname(const char *name, const char *proto)
{
    (void)name; (void)proto;
    return NULL;
}

/* fcntl: c-ares sets a socket non-blocking via fcntl(fd, F_SETFL, flags|O_NONBLOCK). Map that to
 * FreeRTOS+TCP's zero recv/send timeout (= non-blocking); select() then provides readiness. Other
 * commands and non-socket fds are a benign no-op (F_GETFL returns 0; c-ares ORs in O_NONBLOCK). */
int fcntl(int fd, int cmd, ...)
{
    Socket_t s = sock(fd);

    if (cmd == F_SETFL) {
        va_list ap;
        int     arg;
        va_start(ap, cmd);
        arg = va_arg(ap, int);
        va_end(ap);
        if (s != NULL && (arg & O_NONBLOCK)) {
            TickType_t z = 0;
            (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_RCVTIMEO, &z, sizeof(z));
            (void)FreeRTOS_setsockopt(s, 0, FREERTOS_SO_SNDTIMEO, &z, sizeof(z));
        }
    }
    return 0;
}

/* writev: c-ares uses it for TCP DNS sends. Walk the iovec, send() each chunk. */
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    int     i;

    for (i = 0; i < iovcnt; i++) {
        ssize_t n;
        if (iov[i].iov_len == 0) { continue; }
        n = send(fd, iov[i].iov_base, iov[i].iov_len, 0);
        if (n < 0) { return (total > 0) ? total : -1; }
        total += n;
        if ((size_t)n < iov[i].iov_len) { break; }   /* short write -> stop */
    }
    return total;
}
