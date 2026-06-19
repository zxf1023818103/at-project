/*
 * <sys/uio.h> — struct iovec (newlib ships none). Used by <sys/socket.h>'s msghdr and
 * iperf3's scatter/gather paths. readv/writev are declared for completeness; the shim's
 * sendmsg/recvmsg walk the iovec directly.
 */
#ifndef _AT_SYS_UIO_H
#define _AT_SYS_UIO_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#ifdef __cplusplus
}
#endif

#endif /* _AT_SYS_UIO_H */
