# bflb_rfparam — RF parameter / TLV adapter
# (extracted from platform/bouffalo_sdk/drivers/rfparam).

if("${CHIP}" STREQUAL "bl602")
    set(BFLB_RFPARAM_ADAPTER Src/${CHIP}/rfparam_adapter.c)
else()
    set(BFLB_RFPARAM_ADAPTER Src/rfparam_adapter.c)
endif()

add_library(bflb_rfparam STATIC
    ${BFLB_RFPARAM_ADAPTER}
    Src/rfparam_rftlv.c
)

target_include_directories(bflb_rfparam PUBLIC ${CMAKE_CURRENT_LIST_DIR}/Inc)

if(CONFIG_RFPARAM_TLV_FIXED_ADDRESS)
    target_compile_definitions(bflb_rfparam PUBLIC CONFIG_RFPARAM_TLV_FIXED_ADDRESS)
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_rfparam PRIVATE sdk_intf_lib)
endif()
