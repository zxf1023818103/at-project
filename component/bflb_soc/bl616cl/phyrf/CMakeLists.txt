# bflb_soc_bl616cl_phyrf — BL616CL PHY/RF prebuilt binary
# (extracted from platform/bouffalo_sdk/drivers/soc/bl616cl/phyrf).

if(CONFIG_MFG_SDIO OR CONFIG_MFG_USB)
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl616cl_phyrf4wlan_mfg.a)
elseif(CONFIG_PHY_LPFW)
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl616cl_phyrf_lpfw.a)
else()
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl616cl_phyrf.a)
endif()

add_library(bflb_soc_bl616cl_phyrf INTERFACE)
target_link_libraries(bflb_soc_bl616cl_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a})
target_include_directories(bflb_soc_bl616cl_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

if(CONFIG_INCLUDE_PHYRF_HEADER)
    target_include_directories(bflb_soc_bl616cl_phyrf INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/reg
        ${CMAKE_CURRENT_LIST_DIR}/reg/mac154
        ${CMAKE_CURRENT_LIST_DIR}/reg/rf
        ${CMAKE_CURRENT_LIST_DIR}/reg/phybz
        ${CMAKE_CURRENT_LIST_DIR}/reg/wlan11)
endif()

set(BFLB_SOC_BL616CL_PHYRF_STATIC ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a} PARENT_SCOPE)
