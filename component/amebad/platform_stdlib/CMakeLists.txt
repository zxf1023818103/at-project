# component/amebad/platform_stdlib — the RTL8721D libc-retarget header
# (platform_stdlib_rtl8721d.h): redirects printf/mem*/str* to the ROM swlib and
# malloc/free/calloc/realloc to the FreeRTOS heap primitives. Split out of the
# syscalls component into its own header-only component (the matching pattern as
# amebad_dlist). Consumed via `#include "platform_stdlib_rtl8721d.h"` across the
# WiFi / BT / FreeRTOS-glue / app sources, reached through amebad_sdk_includes
# (which links this). Header-only -> INTERFACE, not chip-gated (both cores).
#
# Note: the actual libc retarget *implementation* (newlib syscalls + the strong
# malloc family this header's macros map onto) lives in the syscalls component
# (amebad_syscalls); this is just the header those TUs include.
add_library(amebad_platform_stdlib INTERFACE)
target_include_directories(amebad_platform_stdlib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
