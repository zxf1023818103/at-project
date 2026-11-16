# One NAN/PASN/TCP implementation, with a platform-specific radio transport.
set(NAN_HOST_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(nan_add_freertos_host target)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_subdirectory(${NAN_HOST_SOURCE_DIR}/hostap ${CMAKE_CURRENT_BINARY_DIR}/hostap)
    add_library(${target} STATIC
        ${NAN_HOST_SOURCE_DIR}/nan_service.c ${NAN_HOST_SOURCE_DIR}/nan_network.c
        ${NAN_HOST_SOURCE_DIR}/nan_wire.c ${NAN_HOST_SOURCE_DIR}/nan_tcp_protocol.c)
    target_include_directories(${target} PUBLIC ${NAN_HOST_SOURCE_DIR}
        PRIVATE ${NAN_HOST_SOURCE_DIR}/vendor)
    target_link_libraries(${target} PRIVATE FreeRTOS monitor freertos_plus_tcp mbedcrypto green2_pasn)
    if(AT_NAN_NET_DIAG)
        target_compile_definitions(${target} PRIVATE AT_NAN_NET_DIAG)
    endif()
endfunction()
