# component/bouffalo_sdk/board_flash_port - Bouffalo (BL602/BL616/BL616CL) impl of the cross-platform
# flash write-protect (component/flash_cli_config, board_flash_lock.h): board_flash_protect /
# board_flash_read_status over the LHAL bflb_sflash. Linked into the bouffalo_sdk umbrella; the shared
# `flash` command + helpers (flash_cli) + the at:: C++ wrapper (BoardFlashLock.cpp) call into it. The
# bflb_flash/bflb_sflash symbols resolve from bflb_lhal at the image link (forward-declared here).
add_library(board_flash_port STATIC board_flash_bouffalo.c)
target_link_libraries(board_flash_port PRIVATE board_flash_lock)   # board_flash_lock.h (contract header)
# The chip macro (BL602 / BL616 / BL616CL) - this lib isn't under the scope that gets the global one,
# and board_flash_bouffalo.c's XIP-quiesce path is #if defined(BL602). Derive it from BOUFFALO_CHIP.
string(TOUPPER "${BOUFFALO_CHIP}" _board_flash_chip)
target_compile_definitions(board_flash_port PRIVATE ${_board_flash_chip})
