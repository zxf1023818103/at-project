# amebad_at_cmd — the AT log / command service for the KM4 image2, basic
# functionality only.
#
#   log_service.c     the AT log service: console I/O, the init_database, and
#                     the built-in at_log_* commands (log_service_init / log_buf
#                     / log_rx_interrupt_sema — referenced by the monitor shell)
#   at_cmd_stubs.c    weak stubs for at_sys_init / at_mp_init (the AT+ command
#                     sets log_service registers but that we don't pull for the
#                     minimal build — atcmd_sys.c cascades into the mbed gpio/
#                     adc/sys HAL; atcmd_mp.c is manufacturing test).
#
# log_service.c registers command tables conditionally on CONFIG_WLAN / CONFIG_BT
# (from the SDK's platform_opts.h). KM4 only.

# Header-only INTERFACE (before the chip gate so it exists on BOTH cores). Linked by
# amebad_sdk_includes instead of a raw target_include_directories.
add_library(amebad_at_cmd_headers INTERFACE)
target_include_directories(amebad_at_cmd_headers SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# log_service.c vendored out of the SDK (api/at_cmd); at_cmd_stubs.c is our own
# weak-stub source. The at_cmd / lwip *headers* still come from the SDK.
add_library(amebad_at_cmd STATIC
    log_service.c
    at_cmd_stubs.c
)

# log_service.c -> atcmd_wifi.h/atcmd_lwip.h -> netif_shim.h: only needs
# NET_IF_NUM from the netif shim now. lwip is gone — the transparent-socket AT
# commands (the only lwip-header consumers) are ATVER_2-only and not built here
# (CONFIG_EXAMPLE_UART_ATCMD=0), so their lwip includes are #if'd out.
target_include_directories(amebad_at_cmd SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/component/amebad/freertos_tcp_port")  # netif_shim.h (vendored shim, now in the freertos_tcp port)

target_link_libraries(amebad_at_cmd PUBLIC FreeRTOS)

target_compile_options(amebad_at_cmd PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
