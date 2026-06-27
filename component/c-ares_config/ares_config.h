/* ares_config.h — hand-written c-ares config for the AmebaD KM4 (newlib + FreeRTOS+TCP, GCC).
 *
 * Replaces the autotools/CMake feature-probe output: we have no host probe for a bare-metal
 * sysroot, so the platform capabilities are stated directly. Paired with ares_build.h (copied
 * from c-ares/include/ares_build.h.dist, whose __GNUC__ branch already fits us) and selected via
 * -DHAVE_CONFIG_H (see component/c-ares_config/CMakeLists.txt). Sockets/select come from the
 * component/syscall_config BSD shim; non-blocking is handled by our ares_set_socket_functions
 * override (so deliberately NO HAVE_FCNTL_O_NONBLOCK / HAVE_IOCTL_FIONBIO). DNS servers are fed
 * explicitly via ares_set_servers_csv, so the sysconfig/resolv.conf/getifaddrs probes are off.
 */
#ifndef AT_ARES_CONFIG_H
#define AT_ARES_CONFIG_H

/* ---- standard headers newlib provides ---- */
#define HAVE_ASSERT_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MALLOC_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_UIO_H 1   /* component/syscall_config/sys/uio.h (struct iovec) */

/* fcntl(F_SETFL, O_NONBLOCK) is provided for socket fds by the BSD shim (bsd_socket.c). c-ares
 * requires SOME non-blocking method at compile time (else #error); this is it. */
#define HAVE_FCNTL 1
#define HAVE_FCNTL_O_NONBLOCK 1

/* ---- network headers from component/syscall_config ---- */
#define HAVE_ARPA_INET_H 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_NETINET_TCP_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_SELECT_H 1

/* ---- socket calls available via the BSD shim ---- */
#define HAVE_SOCKET 1
#define HAVE_CONNECT 1
#define HAVE_RECV 1
#define HAVE_RECVFROM 1
#define HAVE_SEND 1
#define HAVE_SENDTO 1
#define HAVE_SETSOCKOPT 1
#define HAVE_SELECT 1
#define HAVE_WRITEV 1   /* implemented in bsd_socket.c (loops send); also stops c-ares redefining struct iovec */

/* ---- libc helpers ---- */
#define HAVE_GETENV 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_STRDUP 1
#define HAVE_STRNLEN 1
#define HAVE_STRUCT_TIMEVAL 1

/* ---- IPv6 types (for AAAA records; the addresses are carried, not socketed here) ---- */
#define HAVE_AF_INET6 1
#define HAVE_PF_INET6 1
#define HAVE_STRUCT_IN6_ADDR 1
#define HAVE_STRUCT_SOCKADDR_IN6 1
#define HAVE_STRUCT_SOCKADDR_STORAGE 1
#define HAVE_STRUCT_ADDRINFO 1   /* component/syscall_config/netdb.h provides it */

/* ---- random: no /dev/urandom, no getrandom/arc4random -> c-ares' built-in RNG ---- */
#define CARES_RANDOM_FILE ""

/* ---- single-threaded: we serialize cares_resolve() ourselves (shim select() not reentrant) ---- */
/* (CARES_THREADS deliberately undefined) */

/* recv()/recvfrom()/send() prototypes as seen through the int-fd BSD shim. */
#define RECV_TYPE_ARG1 int
#define RECV_TYPE_ARG2 void *
#define RECV_TYPE_ARG3 size_t
#define RECV_TYPE_ARG4 int
#define RECV_TYPE_RETV ssize_t
#define RECVFROM_TYPE_ARG1 int
#define RECVFROM_TYPE_ARG2 void *
#define RECVFROM_TYPE_ARG3 size_t
#define RECVFROM_TYPE_ARG4 int
#define RECVFROM_TYPE_ARG5 struct sockaddr *
#define RECVFROM_TYPE_ARG6 socklen_t *
#define RECVFROM_QUAL_ARG5
#define RECVFROM_TYPE_RETV ssize_t
#define SEND_TYPE_ARG1 int
#define SEND_TYPE_ARG2 void *
#define SEND_TYPE_ARG3 size_t
#define SEND_TYPE_ARG4 int
#define SEND_TYPE_RETV ssize_t

#endif /* AT_ARES_CONFIG_H */
