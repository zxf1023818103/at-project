# amebad_sdk_includes — chip-aware INTERFACE bundle of all SDK include
# paths that AmebaD vendor headers expect on the search path.
#
# Mirrors IFLAGS from
#   platform/ameba-rtos-d/.../project_lp/asdk/Makefile.include.gen (lines 146-211).
# SDK MISS paths (cmsis/device, fwlib/rom_map_lp, wifi_fw/include,
# swlib/os_dep/*, swlib/std_lib/*, mbed/.../include, $(ROOTDIR)/../git_code/...)
# are skipped — gcc would accept them silently but they don't exist in the
# submodule.
#
# Chip-gated: KM0 uses inc_lp + FreeRTOS RTL8721D_LP; KM4 uses inc_hp +
# FreeRTOS RTL8721D_HP. The other ~30 paths are chip-agnostic.
#
# Members linked via this INTERFACE:
#   - amebad_fwlib_headers      (project-local 69 chip-register / HAL headers)
#   - inc_lp / inc_hp           (Kconfig autoconf + board opts; chip-selected)
#   - ~30 SDK subdirs           (FreeRTOS, swlib, app, common/api, wlan, lwip,
#                                mbedtls, ...)

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_proj_inc_dir   "inc_lp")
    # KM0 port is flat: portable/GCC/RTL8721D_LP/{port.c,portmacro.h}
    set(_freertos_ports "RTL8721D_LP")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_proj_inc_dir   "inc_hp")
    # KM4 port is split for TrustZone-M: portable/GCC/RTL8721D_HP/{non_secure,secure}/...
    # SDK IFLAGS (project_hp/.../Makefile.include.gen:171-172) lists both.
    set(_freertos_ports "RTL8721D_HP/non_secure" "RTL8721D_HP/secure")
else()
    message(FATAL_ERROR
        "amebad_sdk_includes: AMEBAD_CHIP='${AMEBAD_CHIP}' not supported "
        "(expected amebad-km0 or amebad-km4)")
endif()

add_library(amebad_sdk_includes INTERFACE)
target_link_libraries(amebad_sdk_includes INTERFACE amebad_fwlib_headers)

set(_sdk_base "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d")
set(_sdk_soc  "${_sdk_base}/component/soc/realtek/amebad")
set(_os       "${_sdk_base}/component/os")
set(_freertos "${_os}/freertos/freertos_v10.2.0/Source")
set(_common   "${_sdk_base}/component/common")
set(_proj     "${_sdk_base}/project/realtek_amebaD_va0_example")

# Expand chip-specific FreeRTOS port list to absolute paths.
set(_freertos_port_paths)
foreach(_port IN LISTS _freertos_ports)
    list(APPEND _freertos_port_paths "${_freertos}/portable/GCC/${_port}")
endforeach()

target_include_directories(amebad_sdk_includes SYSTEM INTERFACE
    # project-local chip configs (Kconfig autoconf + board opts)
    "${_proj}/inc/${_proj_inc_dir}"
    # OS / FreeRTOS
    "${_os}"
    "${_os}/freertos"
    "${_os}/os_dep/include"
    "${_freertos}/include"
    ${_freertos_port_paths}
    # SoC-level
    "${_sdk_soc}/cmsis"
    "${_sdk_soc}/app/monitor/include"
    "${_sdk_soc}/app/xmodem"
    "${_sdk_soc}/swlib/include"
    "${_sdk_soc}/swlib/string"
    "${_sdk_soc}/misc"
    # common APIs
    "${_common}/api"
    "${_common}/api/platform"
    "${_common}/api/wifi"
    "${_common}/api/network"
    "${_common}/api/network/include"
    "${_common}/api/at_cmd"
    "${_common}/api/wifi/rtw_wpa_supplicant/src"
    "${_common}/test"
    "${_common}/example"
    "${_common}/utilities"
    # WLAN driver headers
    "${_common}/drivers/wlan/realtek/include"
    "${_common}/drivers/wlan/realtek/src/hci"
    "${_common}/drivers/wlan/realtek/src/osdep"
    "${_common}/drivers/wlan/realtek/src/hal"
    "${_common}/drivers/wlan/realtek/src/hal/OUTSRC"
    "${_common}/drivers/wlan/realtek/src/hal/rtl8711b"
    "${_common}/drivers/wlan/realtek/wlan_ram_map/rom"
    # network / lwip 1.4.1
    "${_common}/network"
    "${_common}/network/lwip/lwip_v1.4.1/src/include"
    "${_common}/network/lwip/lwip_v1.4.1/src/include/lwip"
    "${_common}/network/lwip/lwip_v1.4.1/src/include/netif"
    "${_common}/network/lwip/lwip_v1.4.1/src/include/ipv4"
    "${_common}/network/lwip/lwip_v1.4.1/port/realtek"
    "${_common}/network/lwip/lwip_v1.4.1/port/realtek/freertos"
    # INIC
    "${_common}/drivers/inic/rtl8711b"
    # SSL — polarssl + mbedtls (rtl8721d_ota.h default-on HTTPS_OTA_UPDATE
    # pulls mbedtls/version.h into every TU that includes ameba_soc.h)
    "${_common}/network/ssl/polarssl-1.3.8/include"
    "${_common}/network/ssl/ssl_ram_map/rom"
    "${_common}/network/ssl/mbedtls-2.4.0/include"
    "${_common}/network/ssl/mbedtls_ram_map/rom"
    # mbed HAL
    "${_common}/mbed/targets/hal/rtl8721d"
)
