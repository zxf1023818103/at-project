# bl616_phyrf — INTERFACE wrapper around the prebuilt BL616 PHY/RF static lib.
# Extracted from bouffalo_sdk/drivers/soc/bl616/phyrf.
#
# Which .a is picked depends on the build profile:
#   CONFIG_PHY_LPFW -> libbl616_phyrf_lpfw.a  (low-power firmware build)
#   CONFIG_PHY_CLI  -> libbl616_phyrf_mfg.a   (manufacturing / CLI build)
#   default         -> libbl616_phyrf.a       (production)

add_library(bl616_phyrf INTERFACE)

target_include_directories(bl616_phyrf INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(CONFIG_INCLUDE_PHYRF_HEADER)
    target_include_directories(bl616_phyrf INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/reg"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/mac154"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/rf"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/phybz"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/wlan11"
    )
endif()

set(_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib-gcc_10.2.0-toolchain_V2.6.1")
if(CONFIG_PHY_LPFW)
    target_link_libraries(bl616_phyrf INTERFACE "${_lib_dir}/libbl616_phyrf_lpfw.a")
elseif(CONFIG_PHY_CLI)
    target_link_libraries(bl616_phyrf INTERFACE "${_lib_dir}/libbl616_phyrf_mfg.a")
else()
    target_link_libraries(bl616_phyrf INTERFACE "${_lib_dir}/libbl616_phyrf.a")
endif()
