# component/amebad/syscall_port — AmebaD's console backend for the cross-platform
# newlib retarget. The retarget core (the POSIX fd VFS + malloc family + byte-order)
# is the shared component/syscall_config; what stays here is the platform-specific part:
#
#   console_loguart.c   the default backend = the LOGUART ROM console (DiagPutChar/
#                       DiagGetChar), registered as the strong at_console_default_backend
#                       for the shared console dispatcher (component/console).
#   console_lpuart.c    optional override: stdin/stdout -> UART3 (only with the C++
#                       self-test); see below.
#   time64.h            AmebaD newlib time_t shim (consumed by mbed/rtc_api.*; on the
#                       include path via sdk_headers). Stays with this dir.
#
# The mounted-LittleFS hook the VFS needs (vfs_lfs_mount) is provided by the AmebaD
# LittleFS port (component/amebad/lfs_port). app_km4 links BOTH this OBJECT lib and
# the shared `syscall_config` OBJECT lib (force-included so their strong defs win).
#
# KM4-only: the newlib-stdio consumers (BLE / QUIC / monitor shell) are all KM4.

# Header-only INTERFACE (before the chip gate so it exists on BOTH cores — time64.h
# is on the sdk include path KM0+KM4 even though this OBJECT lib is KM4-only). Linked
# by amebad_sdk_includes instead of a raw target_include_directories.
add_library(amebad_syscall_port_headers INTERFACE)
target_include_directories(amebad_syscall_port_headers SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# OBJECT lib (force-included like the core): console_loguart.c's strong
# at_console_default_backend must override the shared console's weak default, which an
# archive member would not be pulled to do (no undefined ref). See component/syscall_config.
add_library(amebad_syscall_port OBJECT console_loguart.c)

# ameba_soc.h (DiagPutChar/DiagGetChar) + console.h (at_console_backend_t). at_console
# is PUBLIC so the shared dispatcher archive propagates to app_km4's image link.
target_link_libraries(amebad_syscall_port PRIVATE amebad_sdk_includes)
target_link_libraries(amebad_syscall_port PUBLIC at_console)

# Optional LPUART console port (console_lpuart.c): redirects stdin/stdout to UART3
# (PB1/PB2) via the fwlib UART HAL (ROM _LONG_CALL_ entries — no extra driver TU),
# leaving stderr on LOGUART. Only built when the C++ self-test is enabled, so the
# console stays on LOGUART for images that don't redirect it. Only needs ameba_soc.h
# (already on the include path via amebad_sdk_includes above).
if(AMEBAD_CPP_SELFTEST)
    target_sources(amebad_syscall_port PRIVATE console_lpuart.c)
endif()
