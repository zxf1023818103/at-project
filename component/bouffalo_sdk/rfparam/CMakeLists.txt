# rfparam — generic RF parameter loader (rftlv-based). Extracted from
# bouffalo_sdk/drivers/rfparam/. Used by BL616 / BL616CL (and the chips
# we don't ship: BL618DG / BL808 / …) — those call rfparam_init() out of
# src/main.cpp. BL602 uses bl602_rfparam (the chip-specific Src/bl602/
# tree, already extracted as its own component).

if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR
        "rfparam: BL602 should link bl602_rfparam, not this component.")
endif()

# rfparam_adapter.c logs via bflb_log_compat.h -> rxi; collapse __FILE__ toward a basename.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/Src/=")

add_library(rfparam STATIC
    Src/rfparam_adapter.c
    Src/rfparam_rftlv.c
)

target_include_directories(rfparam PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Inc"
)

target_link_libraries(rfparam PUBLIC
    bflb_lhal
    ${BOUFFALO_CHIP}_std
    ${BOUFFALO_CHIP}_phyrf
    rxi_log
)

if(CONFIG_RFPARAM_TLV_FIXED_ADDRESS)
    target_compile_definitions(rfparam PUBLIC CONFIG_RFPARAM_TLV_FIXED_ADDRESS)
endif()
