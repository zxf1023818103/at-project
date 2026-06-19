/*
 * <termios.h> — minimal shim. newlib's own <termios.h> #includes <sys/termios.h>,
 * which this newlib build does not ship (a hard compile error). iperf3's iperf_auth.c
 * pulls <termios.h> for iperf_getpass() (no-echo password input) even with SSL off, so
 * provide just struct termios + ECHO + tcgetattr/tcsetattr. The console is not a tty;
 * bsd_socket.c stubs tcgetattr/tcsetattr to no-ops (iperf_getpass is auth-only, unused).
 * Placed in syscall_config's include dir so it resolves ahead of the sysroot copy.
 */
#ifndef _AT_TERMIOS_H
#define _AT_TERMIOS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;

#define NCCS 32
struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
};

/* c_lflag bits (only ECHO is used) */
#define ECHO    0x0008

/* tcsetattr() optional actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int actions, const struct termios *t);

#ifdef __cplusplus
}
#endif

#endif /* _AT_TERMIOS_H */
