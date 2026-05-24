# bflb_lhal_pec_v2 — built from source when both instance/ and lowlevel/ exist,
# otherwise from a prebuilt static archive shipped under lib/. Same name in
# both branches so the parent can link unconditionally.

if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/instance
   AND IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lowlevel)
    add_library(bflb_lhal_pec_v2 STATIC
        lowlevel/bflb_pec_v2.c
        lowlevel/bflb_pec_v2_instr.c
        instance/bflb_pec_common.c
        instance/bflb_pec_pwm.c
        instance/bflb_pec_uart_tx.c
        instance/bflb_pec_uart_rx.c
        instance/bflb_pec_ir.c
        instance/bflb_pec_spi.c
        instance/bflb_pec_i2c.c
        instance/bflb_pec_dpi.c
        instance/bflb_pec_srgb.c
        instance/bflb_pec_qspi_cam.c
    )
    target_include_directories(bflb_lhal_pec_v2 PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/libpec_v2_${BOUFFALO_CHIP}.a")
        message(FATAL_ERROR
            "no libpec_v2_${BOUFFALO_CHIP}.a found under "
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/")
    endif()
    add_library(bflb_lhal_pec_v2 STATIC IMPORTED GLOBAL)
    set_target_properties(bflb_lhal_pec_v2 PROPERTIES
        IMPORTED_LOCATION
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/libpec_v2_${BOUFFALO_CHIP}.a"
    )
endif()
