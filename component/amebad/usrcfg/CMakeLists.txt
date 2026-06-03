# amebad_usrcfg — AmebaD board / chip configuration data (boot config, IPC,
# WiFi, flash, pinmap, sleep, TrustZone, interface config), project-local copies
# of platform/ameba-rtos-d/.../fwlib/usrcfg/. Flat folder; the SDK filename infix
# selects the group (same split the SDK's usrcfg Makefile uses). One chip-gated
# CMakeLists builds three libs:
#   amebad_usrcfg_common  rtl8721d_*.c    board cfg shared by both cores
#   amebad_usrcfg_lp      rtl8721dlp_*.c  KM0-only (flash/intf/pinmap/sleep)
#   amebad_usrcfg_hp      rtl8721dhp_*.c  KM4-only (TrustZone/interface)
#
# amebad_usrcfg_common is not chip-gated (its .c only #include "ameba_soc.h", no
# LP-specific macros) — it is compiled under whichever chip toolchain is active.
# The three are independent (no inter-link); the bootloader / app link lists name
# common + the per-core lib. Target names are kept so those consumers don't change.
#
# NB the rtl8721d_*.c glob matches only the common files — "rtl8721dlp_" /
# "rtl8721dhp_" have no '_' at that position so they're excluded.
#
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/usrcfg/ \
#           component/amebad/usrcfg/

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
else()
    return()
endif()

# Chip-agnostic board config — built for both cores.
file(GLOB _common CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rtl8721d_*.c")
add_library(amebad_usrcfg_common STATIC ${_common})
# OTA region macros (LS_IMG2_OTA{1,2}_ADDR) come from amebad_sdk_includes — see
# its CMakeLists.txt for the AMEBAD_OTA*_ADDR cmake options.
target_link_libraries(amebad_usrcfg_common PUBLIC amebad_sdk_includes)

set(_usrcfg_targets amebad_usrcfg_common)

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    file(GLOB _lp CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rtl8721dlp_*.c")
    add_library(amebad_usrcfg_lp STATIC ${_lp})
    target_link_libraries(amebad_usrcfg_lp PUBLIC amebad_sdk_includes)
    list(APPEND _usrcfg_targets amebad_usrcfg_lp)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    file(GLOB _hp CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rtl8721dhp_*.c")
    add_library(amebad_usrcfg_hp STATIC ${_hp})
    target_link_libraries(amebad_usrcfg_hp PUBLIC amebad_sdk_includes)
    list(APPEND _usrcfg_targets amebad_usrcfg_hp)
endif()

foreach(_t ${_usrcfg_targets})
    target_compile_options(${_t} PRIVATE
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-sign-compare
    )
endforeach()
