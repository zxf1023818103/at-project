/*
 * <poll.h> — iperf3 uses poll() on a single socket for a timeout-bounded non-blocking
 * connect (net.c timeout_connect). Implemented over FreeRTOS_select in bsd_socket.c.
 */
#ifndef _AT_POLL_H
#define _AT_POLL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int nfds_t;

struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLLIN   0x001
#define POLLPRI  0x002
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif /* _AT_POLL_H */
