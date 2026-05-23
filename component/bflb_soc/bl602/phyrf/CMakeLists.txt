# bflb_soc_bl602_phyrf — BL602 PHY/RF prebuilt binary
# (extracted from platform/bouffalo_sdk/drivers/soc/bl602/phyrf).

set(_phyrf_a lib-gcc-10.2.0-rv32imfc-ilp32f-bouffalosdk/libbl602_phyrf.a)

add_library(bflb_soc_bl602_phyrf INTERFACE)
target_link_libraries(bflb_soc_bl602_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a})
target_include_directories(bflb_soc_bl602_phyrf INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

target_compile_definitions(bflb_soc_bl602_phyrf INTERFACE
    WL_WB03=0
    WL_BL602=1
    CFG_PHY_TRPC=1
    CFG_PHY_ADAPT=1
    CFG_PHY_ADAPT_RXMIL=1
    CFG_PHY_ADAPT_RXDD=1
    CFG_PHY_ADAPT_RXGI=0
    CFG_RF_ICAL=1
    CFG_RF_TCAL=1
    CFG_RF_TCAL_TCHECK_ON=0
    CFG_RF_TCAL_HCOMP_ON=1
    CFG_RF_TCAL_SIM=0
    CFG_PHY_LOG_TRPC=0
    CFG_PHY_LOG_ADAPT=0
    CFG_PHY_LOG_PHY=0
    CFG_PHY_LOG_RFC=0
    CFG_CHIP_VERSION=1
)

set(BFLB_SOC_BL602_PHYRF_STATIC ${CMAKE_CURRENT_LIST_DIR}/${_phyrf_a} PARENT_SCOPE)
