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
#
# fwlib_rom_km{4,0}.ld: the mask-ROM addresses of the fwlib HAL functions these
# headers declare (UART_/GPIO_/FLASH_/RCC_/GDMA_/EFUSE_/CPU_ClkGet/Set DiagPrintf ...),
# partitioned out of the per-core rlx8721d_rom_symbol_acut.ld so the HAL symbols
# live with the headers that declare them. Each KM-image link stage cats the one
# for its core (boot/runtime/fault/FFT symbols stay in the core table). Exposed
# via AMEBAD_FWLIB_ROM_KM{4,0}_LD; KM4 ROM addresses != KM0 ROM addresses, hence
# two tables.

add_library(amebad_fwlib_headers INTERFACE)

target_include_directories(amebad_fwlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(AMEBAD_FWLIB_ROM_KM4_LD "${CMAKE_CURRENT_SOURCE_DIR}/fwlib_rom_km4.ld"
    CACHE INTERNAL "AmebaD KM4 (HP) fwlib HAL ROM-symbol PROVIDE linker script")
set(AMEBAD_FWLIB_ROM_KM0_LD "${CMAKE_CURRENT_SOURCE_DIR}/fwlib_rom_km0.ld"
    CACHE INTERNAL "AmebaD KM0 (LP) fwlib HAL ROM-symbol PROVIDE linker script")
