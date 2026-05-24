# bl616cl_phyrf — INTERFACE wrapper around the prebuilt BL616CL PHY/RF static lib.
# Extracted from bouffalo_sdk/drivers/soc/bl616cl/phyrf.
#
# Which .a is picked depends on the build profile:
#   CONFIG_MFG_SDIO|CONFIG_MFG_USB -> libbl616cl_phyrf4wlan_mfg.a  (MFG over SDIO/USB)
#   CONFIG_PHY_LPFW                -> libbl616cl_phyrf_lpfw.a       (low-power firmware build)
#   default                        -> libbl616cl_phyrf.a            (production)

add_library(bl616cl_phyrf INTERFACE)

target_include_directories(bl616cl_phyrf INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(CONFIG_INCLUDE_PHYRF_HEADER)
    target_include_directories(bl616cl_phyrf INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/reg"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/mac154"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/rf"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/phybz"
        "${CMAKE_CURRENT_SOURCE_DIR}/reg/wlan11"
    )
endif()

set(_lib_dir "${CMAKE_CURRENT_SOURCE_DIR}/lib-gcc_10.2.0-toolchain_V2.6.1")
if(CONFIG_MFG_SDIO OR CONFIG_MFG_USB)
    target_link_libraries(bl616cl_phyrf INTERFACE "${_lib_dir}/libbl616cl_phyrf4wlan_mfg.a")
elseif(CONFIG_PHY_LPFW)
    target_link_libraries(bl616cl_phyrf INTERFACE "${_lib_dir}/libbl616cl_phyrf_lpfw.a")
else()
    target_link_libraries(bl616cl_phyrf INTERFACE "${_lib_dir}/libbl616cl_phyrf.a")
endif()
