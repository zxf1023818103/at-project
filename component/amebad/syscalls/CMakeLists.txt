# component/amebad/syscalls — the KM4 image's libc/newlib retarget, all in one TU.
# Consolidated from the former three syscall copies (hci/hci_syscalls.c,
# app_km4/quic_syscalls.c, monitor/low_level_io.c) plus the malloc family + BSD
# byte-order helpers that used to live in ngtcp2/ngtcp2_port.c. Provides:
#   - _write/_read (LOGUART console), _sbrk (small private heap for newlib stdio),
#     and the inert POSIX stubs newlib expects;
#   - a strong malloc/free/calloc/realloc over the FreeRTOS heap;
#   - bare htonl/htons/ntohl/ntohs (-> FreeRTOS+TCP's FreeRTOS_htonl/htons).
#
# OBJECT library, not a static archive: the KM4 image2 link has no --gc-sections,
# but more importantly an archive member is only pulled to satisfy an *undefined*
# ref, and libc.a already carries default ENOSYS _write/_sbrk stubs that would be
# picked first — so our retarget must be force-included (like an executable object)
# for its STRONG defs to win. app_km4 links this OBJECT lib into target_img2_km4.
#
# KM4-only: the newlib-stdio consumers (BLE / QUIC / monitor shell) are all KM4.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# One TU: newlib syscall stubs + the FreeRTOS-heap malloc family + the BSD
# byte-order helpers (htonl/...), the latter two moved here from ngtcp2 so every
# KM4 program shares one copy (see newlib_syscalls.c).
add_library(amebad_syscalls OBJECT newlib_syscalls.c)

# ameba_soc.h -> DiagPutChar / DiagGetChar (ROM console); amebad_freertos_tcp
# carries FreeRTOS.h (pvPortMalloc) + FreeRTOS_IP.h (FreeRTOS_htonl) on its PUBLIC
# include path. It is an OBJECT lib, so this only borrows those include dirs at
# compile time — no archive is pulled in (FreeRTOS_htonl is a header-only macro).
target_link_libraries(amebad_syscalls PRIVATE amebad_sdk_includes amebad_freertos_tcp)
