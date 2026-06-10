# component/amebad/syscalls — the KM4 image's libc/newlib retarget, all in one TU.
# Consolidated from the former three syscall copies (hci/hci_syscalls.c,
# app_km4/quic_syscalls.c, monitor/low_level_io.c) plus the malloc family + BSD
# byte-order helpers that used to live in ngtcp2/ngtcp2_port.c. Provides:
#   - a POSIX fd VFS: _open/_close/_read/_write/_lseek/_fstat/_isatty/_stat/_unlink
#     route by fd — 0/1/2 = LOGUART console, _open'd paths -> LittleFS (lfs_file_*),
#     sockets registered via amebad_fd_open_socket() -> FreeRTOS_recv/send/close;
#   - _sbrk (small private heap for newlib stdio) + the inert POSIX stubs;
#   - a strong malloc/free/calloc/realloc over the FreeRTOS heap;
#   - bare htonl/htons/ntohl/ntohs (-> FreeRTOS+TCP's FreeRTOS_htonl/htons).
#
# OBJECT library, not a static archive: an archive member is only pulled to satisfy
# an *undefined* ref, and libc.a already carries default ENOSYS _write/_sbrk stubs
# that would be picked first — so our retarget must be force-included (like an
# executable object) for its STRONG defs to win. app_km4 links this OBJECT lib into
# target_img2_km4. The VFS code is compiled per-function (-ffunction-sections) so
# --gc-sections (on at the image link) drops it when no consumer calls fopen/open/
# amebad_fd_open_socket — always available, zero cost until used.
#
# KM4-only: the newlib-stdio consumers (BLE / QUIC / monitor shell) are all KM4.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# One TU: the POSIX fd VFS + newlib syscall stubs + the FreeRTOS-heap malloc family
# + the BSD byte-order helpers (htonl/...). See newlib_syscalls.c.
# newlib_syscalls.c = VFS + malloc + byte-order; console_loguart.c = the default
# console backend (LOGUART) for the shared dispatcher (component/console). Both
# always compiled; the console fds (0/1/2) route through at_console_* now.
add_library(amebad_syscalls OBJECT newlib_syscalls.c console_loguart.c)

# Include-path borrows (OBJECT lib -> compile-time only, no archives pulled here;
# the lfs/freertos_tcp archives are already in app_km4's link group):
#   amebad_sdk_includes   -> ameba_soc.h (DiagPutChar/DiagGetChar)
#   amebad_freertos_tcp   -> FreeRTOS.h, FreeRTOS_IP.h (FreeRTOS_htonl), FreeRTOS_Sockets.h
#   amebad_lfs_port       -> lfs_port.h (amebad_lfs_mount) + lfs.h (lfs_file_*/LFS_O_*)
# at_console (PUBLIC) -> console.h + the at_console_read/write archive, propagated
#   to app_km4 so the shared dispatcher is in the final image link.
target_link_libraries(amebad_syscalls PRIVATE
    amebad_sdk_includes amebad_freertos_tcp amebad_lfs_port)
target_link_libraries(amebad_syscalls PUBLIC at_console)

# Optional LPUART console port (console_lpuart.c): redirects stdin/stdout to UART3
# (PB1/PB2) via the fwlib UART HAL (ROM _LONG_CALL_ entries — no extra driver TU),
# leaving stderr on LOGUART. Only built when the C++ self-test is enabled, so the
# console stays on LOGUART for images that don't redirect it. Only needs ameba_soc.h
# (already on the include path via amebad_sdk_includes above).
if(AMEBAD_CPP_SELFTEST)
    target_sources(amebad_syscalls PRIVATE console_lpuart.c)
endif()
