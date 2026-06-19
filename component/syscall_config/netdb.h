/*
 * <netdb.h> — getaddrinfo()/freeaddrinfo()/gai_strerror() for the shim. Implemented in
 * bsd_socket.c: numeric literals via inet_pton, hostnames via FreeRTOS_gethostbyname
 * (IPv4 only). getnameinfo is not used by iperf3 and is omitted.
 */
#ifndef _AT_NETDB_H
#define _AT_NETDB_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo {
    int               ai_flags;
    int               ai_family;
    int               ai_socktype;
    int               ai_protocol;
    socklen_t         ai_addrlen;
    struct sockaddr  *ai_addr;
    char             *ai_canonname;
    struct addrinfo  *ai_next;
};

/* ai_flags */
#define AI_PASSIVE      0x01
#define AI_NUMERICHOST  0x04
#define AI_NUMERICSERV  0x08

/* EAI_* error codes (negative; gai_strerror maps them) */
#define EAI_NONAME   -2
#define EAI_AGAIN    -3
#define EAI_FAIL     -4
#define EAI_FAMILY   -6
#define EAI_MEMORY   -10
#define EAI_SYSTEM   -11

int  getaddrinfo(const char *node, const char *service,
                 const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int errcode);

#ifdef __cplusplus
}
#endif

#endif /* _AT_NETDB_H */
