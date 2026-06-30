# wifi4_wifi_hosal — HOSAL (Host OS Abstraction Layer) shim that exposes
# the wifi_hosal_* / bflb_pm_* APIs board code calls during Wi-Fi bring-up.
# Extracted from
# bouffalo_sdk/components/wireless/wifi4/wifi4_adapter/wifi_hosal/.
#
# Public surface: include/wifi_hosal.h, include/bflb_pm.h.
# Chip port: port/wifi_hosal_bl602.c (bl606p / bl808 variants pruned —
# wifi4 lives on BL602 only in this tree).

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4_wifi_hosal: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

add_library(wifi4_wifi_hosal STATIC
    wifi_hosal.c
    bflb_pm.c
    port/wifi_hosal_bl602.c
)

target_include_directories(wifi4_wifi_hosal PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Needs lhal (clocks, GPIO, IRQ), the firmware archive's CFG_* defines,
# and FreeRTOS. The bflb_pm.c here is wifi_hosal's own tiny power-event
# framework (different "bl_pm" from the SDK's bflb_power_mgmt — confusing
# naming) and is self-contained, so we don't link bflb_power_mgmt
# (that's a BL616+ thing).
target_link_libraries(wifi4_wifi_hosal
    PUBLIC
        wifi4_firmware
        bflb_lhal
        bflb_list
        freertos_kernel
    PRIVATE
        wifi4_internal_headers
)
