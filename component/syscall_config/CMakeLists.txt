# component/syscall_config — cross-platform libc/newlib retarget (extracted from
# component/amebad/syscall_port). One TU provides:
#   - a POSIX fd VFS: _open/_close/_read/_write/_lseek/_fstat/_isatty/_stat/_unlink
#     route by fd — 0/1/2 = the shared console dispatcher (component/console),
#     _open'd paths -> LittleFS (lfs_file_*), sockets registered via fd_open_socket()
#     -> FreeRTOS+TCP (FreeRTOS_recv/send/close);
#   - _sbrk (small private heap for newlib stdio) + the inert POSIX stubs;
#   - a strong malloc/free/calloc/realloc over the FreeRTOS heap;
#   - bare htonl/htons/ntohl/ntohs (-> FreeRTOS+TCP's FreeRTOS_htonl/htons).
#
# Platform-neutral: the only platform hook is vfs_lfs_mount() (the mounted LittleFS;
# the platform's FS port provides the strong def — AmebaD: component/amebad/lfs_port).
# The console backend is registered by the platform via the shared console component's
# weak at_console_default_backend (AmebaD: component/amebad/syscall_port/console_loguart.c).
#
# OBJECT library, not a static archive: an archive member is only pulled to satisfy
# an *undefined* ref, and libc.a already carries default ENOSYS _write/_sbrk stubs
# that would be picked first — so this retarget must be force-included (like an
# executable object) for its STRONG defs to win. The consumer adds it to its link
# (AmebaD: app_km4). The VFS code is compiled per-function (-ffunction-sections) so
# --gc-sections (at the image link) drops it when no consumer calls open/fopen/
# fd_open_socket — always available, zero cost until used.
#
# Built where its deps exist (FreeRTOS heap + littlefs + FreeRTOS+TCP), so it is
# add_subdirectory'd by the consuming platform (AmebaD KM4), not at the repo root —
# freertos-plus-tcp_config is KM4-gated. lfs.h (littlefs) is the only universally-present dep.
add_library(syscall_config OBJECT newlib_syscalls.c env.c time_base.c
            bsd_socket.c)    # BSD-sockets/POSIX-net layer over FreeRTOS-Plus-TCP (socket/select/getaddrinfo/...)

# picolibc leaves stdin/stdout/stderr for the application to define (newlib
# supplies them itself), so a toolchain whose libc is picolibc needs this bridge
# or every link fails with "undefined reference to `stdout'". Set by the triple
# toolchain file; today only bl618dg's riscv64-zephyr-elf, which is mandatory
# there because that chip's wireless libs are GCC-14 slim-LTO bytecode.
if(AT_LIBC_PICOLIBC)
    target_sources(syscall_config PRIVATE picolibc_stdio.c)
endif()

# pthread_shim.c — the pthread slice iperf3 needs, over FreeRTOS tasks/mutexes. Its
# handle cast (*m = (pthread_mutex_t)(uintptr_t)sem) assumes newlib makes pthread_mutex_t
# an integer handle — true on AmebaD's ARM newlib, but Bouffalo's t-head newlib makes it
# an aggregate struct, so this TU won't compile there. iperf3 is AmebaD-only, so gate it
# out on Bouffalo — the single platform-specific seam in this otherwise neutral component.
if(NOT BOUFFALO_CHIP)
    target_sources(syscall_config PRIVATE pthread_shim.c)
endif()

# PUBLIC: syscalls.h (vfs_lfs_mount / fd_open_socket / env_init hooks) AND our
# self-contained <dirent.h> (newlib's <sys/dirent.h> is a `#error` stub) — placed
# on the include path AHEAD of the sysroot so `#include <dirent.h>` resolves here
# for env.c, the dir VFS, and any consumer (e.g. mbedTLS x509_crt.c under FS_IO).
target_include_directories(syscall_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Header-only borrows (OBJECT lib -> compile-time include paths only; the real
# archives are already in the consumer's link group):
#   FreeRTOS            -> FreeRTOS.h / semphr.h (heap + the fd-table mutex)
#   littlefs            -> lfs.h (lfs_t / lfs_file_* / LFS_O_*)
#   freertos-plus-tcp_config -> FreeRTOS_IP.h / FreeRTOS_Sockets.h (htonl + the socket
#                          backend); it owns the FreeRTOS+TCP include surface.
# at_console (PUBLIC) -> console.h (at_console_read/write); propagated to the consumer
#   so the shared dispatcher archive is in the final image link.
target_link_libraries(syscall_config PRIVATE FreeRTOS littlefs freertos-plus-tcp_config)
target_link_libraries(syscall_config PUBLIC at_console)
