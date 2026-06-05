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
# "Basic only": the config/ override (first on the include path) forces
# CONFIG_WLAN / CONFIG_BT off, so log_service.c doesn't register the wifi/bt/
# mqtt command tables (which would pull the whole network stack). The result is
# the AT log framework running with the built-in log commands; richer AT+ sets
# get wired in (with their HAL) later. KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# log_service.c vendored out of the SDK (api/at_cmd); at_cmd_stubs.c is our own
# weak-stub source. The at_cmd / lwip *headers* still come from the SDK.
add_library(amebad_at_cmd STATIC
    log_service.c
    at_cmd_stubs.c
)

# config/ FIRST so its platform_opts.h (#include_next + CONFIG_WLAN/BT=0)
# intercepts the SDK's. Then FreeRTOS headers + the rest via sdk_includes.
target_include_directories(amebad_at_cmd BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../config")

# log_service.c -> atcmd_wifi.h/atcmd_lwip.h -> lwip_netconf.h: only needs
# NET_IF_NUM from the lwip-free shim now. lwip is gone — the transparent-socket AT
# commands (the only lwip-header consumers) are ATVER_2-only and not built here
# (CONFIG_EXAMPLE_UART_ATCMD=0), so their lwip includes are #if'd out.
target_include_directories(amebad_at_cmd SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/component/amebad/lwip/config")  # lwip_netconf.h (vendored shim)

target_link_libraries(amebad_at_cmd PUBLIC FreeRTOS)

target_compile_options(amebad_at_cmd PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
