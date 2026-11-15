# The upstream kernel is shared; only the Realtek interrupt/context ABI is local.
add_library(freertos_kernel_port_headers INTERFACE)
target_include_directories(freertos_kernel_port_headers INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/${AMEBADPLUS_CORE})
add_library(freertos_kernel_port OBJECT ${AMEBADPLUS_CORE}/port.c)
if(AMEBADPLUS_CORE STREQUAL "km4")
    target_sources(freertos_kernel_port PRIVATE km4/portasm.c)
endif()
target_link_libraries(freertos_kernel_port PUBLIC freertos_kernel_port_headers
    PRIVATE freertos_config amebadplus_sdk)
target_include_directories(freertos_kernel_port PRIVATE
    ${AT_ROOT}/component/freertos-kernel_config/freertos-kernel/include)
target_link_libraries(freertos_config PUBLIC freertos_kernel_port_headers amebadplus_sdk)

target_include_directories(freertos_config PUBLIC ${AT_ROOT}/component/freertos-kernel_config/freertos-kernel/include)
