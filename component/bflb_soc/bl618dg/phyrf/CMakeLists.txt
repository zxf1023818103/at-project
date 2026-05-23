# bflb_soc_bl618dg_phyrf — BL618DG PHY/RF prebuilt binary
# (extracted from platform/bouffalo_sdk/drivers/soc/bl618dg/phyrf).

if("${CPU_MODEL}" STREQUAL "b0")
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl618dg_phyrf_b0.a)
elseif(CONFIG_PHY_CLI)
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl618dg_phyrf_4mfg.a)
else()
    set(_phyrf_a lib-gcc_10.2.0-toolchain_V2.6.1/libbl618dg_phyrf.a)
endif()

add_library(bflb_soc_bl618dg_phyrf INTERFACE)
target_link_libraries(bflb_soc_bl618dg_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a})
target_include_directories(bflb_soc_bl618dg_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

set(BFLB_SOC_BL618DG_PHYRF_STATIC ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a} PARENT_SCOPE)
