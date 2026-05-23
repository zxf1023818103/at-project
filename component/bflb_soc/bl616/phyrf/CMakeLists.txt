# bflb_soc_bl616_phyrf — BL616 PHY/RF prebuilt binary
# (extracted from platform/bouffalo_sdk/drivers/soc/bl616/phyrf).

if(CONFIG_PHY_LPFW)
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl616_phyrf_lpfw.a)
elseif(CONFIG_PHY_CLI)
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl616_phyrf_mfg.a)
else()
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl616_phyrf.a)
endif()

add_library(bflb_soc_bl616_phyrf INTERFACE)
target_link_libraries(bflb_soc_bl616_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a})
target_include_directories(bflb_soc_bl616_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

if(CONFIG_INCLUDE_PHYRF_HEADER)
    target_include_directories(bflb_soc_bl616_phyrf INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/reg
        ${CMAKE_CURRENT_LIST_DIR}/reg/mac154
        ${CMAKE_CURRENT_LIST_DIR}/reg/rf
        ${CMAKE_CURRENT_LIST_DIR}/reg/phybz
        ${CMAKE_CURRENT_LIST_DIR}/reg/wlan11)
endif()

# Expose the .a path so the SDK aggregation step can whole-archive it.
set(BFLB_SOC_BL616_PHYRF_STATIC ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a} PARENT_SCOPE)
