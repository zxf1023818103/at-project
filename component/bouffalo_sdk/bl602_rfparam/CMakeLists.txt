# bl602_rfparam — RF parameter / calibration TLV reader for BL602.
# Extracted from bouffalo_sdk/drivers/rfparam (BL602 variant: Src/bl602/
# rfparam_adapter.c + the chip-agnostic rfparam_rftlv.c; Inc/ headers folded
# into the root).
#
# Deps:
#   bl602_std    — bl602_aon.h, bl602_hbn.h
#   bl602_phyrf  — phy_trpc.h, wl_api.h
#   bflb_log     — log.h
#   FreeRTOS     — FreeRTOS.h, portable.h (consumer must link a FreeRTOS
#                  target separately; we don't pull it in here so a
#                  non-RTOS build of bl602_rfparam stays an opt-in choice).

add_library(bl602_rfparam STATIC
    rfparam_adapter.c
    rfparam_rftlv.c
)

target_include_directories(bl602_rfparam PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(bl602_rfparam PUBLIC
    bl602_std
    bl602_phyrf
    bflb_log
)

if(CONFIG_RFPARAM_TLV_FIXED_ADDRESS)
    target_compile_definitions(bl602_rfparam PUBLIC CONFIG_RFPARAM_TLV_FIXED_ADDRESS)
endif()
