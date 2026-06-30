/*
 * <net/if.h> — interface name helpers. iperf3 only needs IFNAMSIZ and if_nametoindex()
 * (for --bind-dev, which this port does not support: if_nametoindex returns 0).
 */
#ifndef _AT_NET_IF_H
#define _AT_NET_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ 16

unsigned int if_nametoindex(const char *ifname);
char        *if_indextoname(unsigned int ifindex, char *ifname);

#ifdef __cplusplus
}
#endif

#endif /* _AT_NET_IF_H */
