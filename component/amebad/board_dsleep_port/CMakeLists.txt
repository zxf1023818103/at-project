# component/amebad/board_dsleep_port - AmebaD (RTL8721D KM4) impl of board_dsleep_enter for the shared
# `dsleep` command (component/dsleep_config, dsleep.h). AmebaD deep sleep (SOCPS) can't wake on UART,
# so this uses the LP retention tier (SleepCG) via the pmu/tickless mechanism (LOGUART RX wakes it,
# resume-in-place). Mirrors component/amebad/board_flash_port. KM4-only.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(board_dsleep_port STATIC board_dsleep_amebad.c)

# ameba_soc.h (pmu_* / SLEEP_CG / PMU_OS) + FreeRTOS + dsleep.h (contract header) + monitor
# (monitor.h + monitor_rx_get: the console RX primitive we block on for UART wake).
target_link_libraries(board_dsleep_port PRIVATE amebad_sdk_includes FreeRTOS board_dsleep monitor)
