/*
 * <netinet/tcp.h> — TCP-level setsockopt names for the FreeRTOS-Plus-TCP shim.
 * Only TCP_NODELAY is defined (accepted-and-ignored, since FreeRTOS-Plus-TCP has no
 * Nagle toggle); the Linux-specific options stay undefined so iperf3's feature
 * "ifdef TCP_xxx" branches compile out.
 */
#ifndef _AT_NETINET_TCP_H
#define _AT_NETINET_TCP_H

#define TCP_NODELAY 0x01
#define TCP_MAXSEG  0x02   /* accepted-and-ignored: FreeRTOS-Plus-TCP MSS is fixed */

#endif /* _AT_NETINET_TCP_H */
