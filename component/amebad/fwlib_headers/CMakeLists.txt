# amebad_fwlib_headers — Realtek AmebaD (RTL8721D) chip fwlib headers.
#
# Pure INTERFACE library: 69 .h files, no .c. Provides chip register
# definitions, HAL inlines, and the `ameba_soc.h` umbrella header that
# every AmebaD source file pulls in (bootloader, fwlib, drivers, app).
#
# Project-local copy of
#   platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/include/
# extracted so the AmebaD CMake build doesn't reach into the SDK
# submodule for headers. Resync with the SDK when the submodule
# pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/include/ \
#           component/amebad/fwlib_headers/
# (and consider whether divergences are intentional patches or
# upstream drift before merging).
#
# Marked SYSTEM so -Wall / -Wextra / -Wpointer-arith on the consuming
# target don't fire on Realtek-vendor code we don't own.

add_library(amebad_fwlib_headers INTERFACE)

target_include_directories(amebad_fwlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
