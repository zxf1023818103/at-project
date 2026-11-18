# component/bouffalo_sdk/board_dsleep_port - Bouffalo (BL616/BL602) impl of board_dsleep_enter for the
# shared `dsleep` command (component/dsleep_config, dsleep.h): a full PDS deep sleep (reset on wake)
# armed with a UART-RX pad edge and/or the PDS 32 kHz timer. bl616 = pm_pds_mode_enter + PDS-GPIO-INT
# on GPIO22; bl602 = PDS_Mode_Enter(PDS_APP_CFG_Type) + AON GPIO7. The pm_pds/PDS_* symbols resolve
# from the chip std at the image link. Mirrors board_flash_port.
add_library(board_dsleep_port STATIC board_dsleep_bouffalo.c)
target_link_libraries(board_dsleep_port PRIVATE board_dsleep "${BOUFFALO_CHIP}_std")   # dsleep.h + PDS/HBN/AON/pm headers

# The chip macro (BL602 / BL616 / BL616CL) - board_dsleep_bouffalo.c is #if-split on it and this lib
# is outside the scope that gets the global one. Derive it from BOUFFALO_CHIP (as board_flash_port does).
string(TOUPPER "${BOUFFALO_CHIP}" _board_dsleep_chip)
target_compile_definitions(board_dsleep_port PRIVATE ${_board_dsleep_chip})
