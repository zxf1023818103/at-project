sdk_generate_library()
sdk_library_add_sources(net_al_tcp.c)
sdk_add_include_directories(.)
# Headers are needed while the SDK's Wi-Fi targets are configured, before the
# application adds the FreeRTOS+TCP library itself.
sdk_add_include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/../freertos-plus-tcp_config/freertos-plus-tcp/source/include
    ${CMAKE_CURRENT_LIST_DIR}/../freertos-plus-tcp_config/freertos-plus-tcp/source/portable/Compiler/GCC
)
sdk_add_include_directories(${SDK_DEMO_PATH})
