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
# "Basic only": the config_km4/ override (first on the include path) forces
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

# config_km4/ FIRST so its platform_opts.h (#include_next + CONFIG_WLAN/BT=0)
# intercepts the SDK's. Then FreeRTOS headers + the rest via sdk_includes.
target_include_directories(amebad_at_cmd BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../config_km4")

# log_service.c -> lwip_netconf.h -> tcpip.h: a header-only dependency on lwip
# (it registers, but with CONFIG_WLAN off doesn't *call*, the network AT
# commands). KM4 uses lwip v2.2.0 (sdk_includes carries the KM0 v1.4.1 paths),
# so add the v2.2.0 search paths here. project_hp Makefile.include.gen:232-237.
set(_lwip "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/common/network/lwip/lwip_v2.2.0")
target_include_directories(amebad_at_cmd SYSTEM PRIVATE
    "${_lwip}/src/include"
    "${_lwip}/src/include/lwip"
    "${_lwip}/src/include/netif"
    "${_lwip}/port/realtek"
    "${_lwip}/port/realtek/freertos")

target_link_libraries(amebad_at_cmd PUBLIC FreeRTOS)

target_compile_options(amebad_at_cmd PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
