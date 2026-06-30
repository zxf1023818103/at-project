# wifi4_bflb_adapter — Bouffalo platform / OS / CLI / iperf glue that the
# Wi-Fi stack (manager + supplicant + firmware) calls into. Extracted from
# bouffalo_sdk/components/wireless/wifi4/wifi4_adapter/bflb_adapter/.
#
# The four PALs (platform / RTOS / CLI / net-iperf) define the "abstraction
# layer" headers everyone else #includes; bflb_os_hal.c plugs them into the
# Bouffalo SDK FreeRTOS world. blcrypto_sec_pka is a thin PKA helper used
# by the supplicant's ECC paths.
#
# CFG_FREERTOS + BOUFFALO_SDK are the two PUBLIC macros consumers (manager,
# supplicant, hosal) #ifdef on to pick the FreeRTOS-on-bouffalo backend.

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4_bflb_adapter: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

# CLI glue (cli_al.c) has been removed from this component — the CLI
# layer wasn't part of the driver core and pulled in utils_getopt + a
# shell. Iperf glue is opt-in because it needs lwip sockets, which
# aren't wired into the chip-toolchain component/ tree yet.
option(WIFI4_BFLB_ADAPTER_IPERF "Build net_iperf_al.c (needs lwip)" OFF)

add_library(wifi4_bflb_adapter STATIC
    bflb_os_adapter/bflb_os_hal.c
    plat_bouffalo_sdk.c
    rtos_al.c
    tlv/utils_tlv_bl.c
    blcrypto_sec_pka/blcrypto_sec_pka.c
)

if(WIFI4_BFLB_ADAPTER_IPERF)
    target_sources(wifi4_bflb_adapter PRIVATE net_iperf_al.c)
endif()

target_include_directories(wifi4_bflb_adapter PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_os_adapter/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_os_adapter/include/bflb_os_adapter"
    "${CMAKE_CURRENT_SOURCE_DIR}/bflb_os_adapter"
    "${CMAKE_CURRENT_SOURCE_DIR}/tlv"
    "${CMAKE_CURRENT_SOURCE_DIR}/blcrypto_sec_pka"
)

target_compile_definitions(wifi4_bflb_adapter PUBLIC
    CFG_FREERTOS
    BOUFFALO_SDK
)

# OS-HAL needs FreeRTOS, PKA needs lhal/std register access. net_iperf
# also needs lwIP sockets — same lwIP-handled-by-consumer convention as
# wifi4_manager applies here. Everything depends on wifi4_firmware for
# its CFG_*/RW_* defines, and plat_bouffalo_sdk.c hooks into the async
# event bus via bflb_async_event's async_event.h.
target_link_libraries(wifi4_bflb_adapter
    PUBLIC
        wifi4_firmware
        bflb_lhal
        bflb_log
        bflb_async_event
        freertos_kernel
    PRIVATE
        wifi4_internal_headers
)
