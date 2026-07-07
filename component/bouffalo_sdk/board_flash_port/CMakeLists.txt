# component/bouffalo_sdk/board_flash_port - Bouffalo impl of the cross-platform flash write-protect
# (component/flash_cli_config, board_flash_lock.h): board_flash_protect / board_flash_read_status over
# the LHAL bflb_sflash. Linked into the bouffalo_sdk umbrella; the shared `flash` command + helpers
# (flash_cli) + the at:: C++ wrapper (BoardFlashLock.cpp) call into it. The bflb_flash/bflb_sflash
# symbols resolve from bflb_lhal at the image link (forward-declared here, no header dep).
add_library(board_flash_port STATIC board_flash_bl602.c)
target_include_directories(board_flash_port PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../flash_cli_config")
# The chip macro (BL602 / BL616 / BL616CL) - this lib isn't under the scope that gets the global one,
# and board_flash_bl602.c's XIP-quiesce path is #if defined(BL602). Derive it from BOUFFALO_CHIP.
string(TOUPPER "${BOUFFALO_CHIP}" _board_flash_chip)
target_compile_definitions(board_flash_port PRIVATE ${_board_flash_chip})
