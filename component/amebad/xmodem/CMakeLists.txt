# component/amebad/xmodem — AmebaD KM0 mask-ROM xmodem image-download API.
#
# Header-only (xmodem_rom.h / xmodem_uart_rom.h / xmodem_update_rom.h declare the
# ROM functions; the boot ROM implements the actual UART xmodem download). This
# component owns both the headers and the ROM symbol table:
#
#   amebad_xmodem (INTERFACE)      exposes the xmodem_*.h headers. amebad_sdk_includes
#                                  links it, so every consumer keeps getting them.
#   xmodem.ld                      the ~16 ROM symbol addresses (xmodem_* + ctrl/uartx),
#                                  PROVIDE'd; exposed via AMEBAD_XMODEM_ROM_SYMBOL_LD and
#                                  cat'd into every KM0-image combined linker script
#                                  (bootloader_km0 / bootloader_km0_2nd / app_km0).

add_library(amebad_xmodem INTERFACE)
target_include_directories(amebad_xmodem SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")   # xmodem_rom.h / xmodem_uart_rom.h / xmodem_update_rom.h

# Absolute path so KM0 link stages can cat it and list it in their DEPENDS.
# CACHE INTERNAL -> visible across subdirs (mirrors AMEBAD_COMMON_ROM_SYMBOL_LD).
set(AMEBAD_XMODEM_ROM_SYMBOL_LD
    "${CMAKE_CURRENT_SOURCE_DIR}/xmodem.ld"
    CACHE INTERNAL "AmebaD KM0 ROM xmodem-symbol PROVIDE linker script")
