/*
 * <sys/utsname.h> — uname() for iperf3's print_system_info(). Filled with the
 * AmebaD/FreeRTOS identity in bsd_socket.c.
 */
#ifndef _AT_SYS_UTSNAME_H
#define _AT_SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

#define _UTSNAME_LENGTH 65
struct utsname {
    char sysname[_UTSNAME_LENGTH];
    char nodename[_UTSNAME_LENGTH];
    char release[_UTSNAME_LENGTH];
    char version[_UTSNAME_LENGTH];
    char machine[_UTSNAME_LENGTH];
};

int uname(struct utsname *buf);

#ifdef __cplusplus
}
#endif

#endif /* _AT_SYS_UTSNAME_H */
