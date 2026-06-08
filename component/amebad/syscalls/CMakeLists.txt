# component/amebad/syscalls — the KM4 image's newlib syscall retarget, consolidated
# from the former three copies (hci/hci_syscalls.c, app_km4/quic_syscalls.c,
# monitor/low_level_io.c). Provides _write/_read (LOGUART console), _sbrk (a small
# private heap for newlib stdio) and the inert POSIX stubs newlib expects.
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

add_library(amebad_syscalls OBJECT newlib_syscalls.c)

# ameba_soc.h -> DiagPutChar / DiagGetChar (ROM console).
target_link_libraries(amebad_syscalls PRIVATE amebad_sdk_includes)
