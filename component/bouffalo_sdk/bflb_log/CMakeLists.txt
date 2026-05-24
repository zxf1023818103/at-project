# bflb_log — Bouffalo logging utility.
# Extracted from bouffalo_sdk/components/utils/log (the nested bflb_log/
# subdir is flattened — all .c/.h sit at the same level here).
#
# log_freertos.c is the RTOS-aware backend (uses FreeRTOS queue/semphr/event
# groups for thread-safe async logging). Off by default — flip the option
# ON and link a FreeRTOS target separately when integrating into a real
# firmware build.

option(BFLB_LOG_FREERTOS "Build the FreeRTOS-aware log backend (log_freertos.c)" OFF)

add_library(bflb_log STATIC
    log.c
    bflb_log.c
)

if(BFLB_LOG_FREERTOS)
    target_sources(bflb_log PRIVATE log_freertos.c)
endif()

target_include_directories(bflb_log PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# log.c #includes bflb_uart.h from lhal.
target_link_libraries(bflb_log PUBLIC bflb_lhal)
