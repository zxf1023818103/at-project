# component/amebad/board_flash_port — AmebaD (RTL8721D KM4) impl of the cross-platform flash
# write-protect (component/flash_cli_config, board_flash_lock.h): board_flash_protect /
# board_flash_read_status over the Realtek FLASH_* fwlib (SR1 BP bits + SR2 CMP). The shared
# `flash` command + helpers (flash_cli) call into it — strong refs, so the archive member is
# pulled wherever flash_cli links. Mirrors component/bouffalo_sdk/board_flash_port. KM4-only.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(board_flash_port STATIC board_flash_amebad.c)

# ameba_soc.h (FLASH_* fwlib + flash_init_para) + board_flash_lock.h.
target_link_libraries(board_flash_port PRIVATE amebad_sdk_includes FreeRTOS board_flash_lock)  # board_flash_lock.h
