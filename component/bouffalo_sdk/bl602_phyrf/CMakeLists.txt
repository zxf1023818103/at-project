# bl602_phyrf — INTERFACE wrapper around the prebuilt BL602 PHY/RF static lib.
# Extracted from bouffalo_sdk/drivers/soc/bl602/phyrf.
#
# Two other -bouffalosdk-less lib variants ship in the source dir
# (gcc-8.3.0 / gcc-10.2.0 without "bouffalosdk" suffix); the SDK picks the
# "bouffalosdk" variant, so we mirror that.

add_library(bl602_phyrf INTERFACE)

target_include_directories(bl602_phyrf INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(bl602_phyrf INTERFACE
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

target_link_libraries(bl602_phyrf INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib-gcc-10.2.0-rv32imfc-ilp32f-bouffalosdk/libbl602_phyrf.a"
)
