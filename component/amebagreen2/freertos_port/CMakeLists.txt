# The upstream kernel is shared; only the Realtek interrupt/context ABI is local.
add_library(freertos_kernel_port_headers INTERFACE)
target_include_directories(freertos_kernel_port_headers INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/${AMEBAGREEN2_CORE})
add_library(freertos_kernel_port OBJECT ${AMEBAGREEN2_CORE}/port.c ${AMEBAGREEN2_CORE}/portasm.c)
target_link_libraries(freertos_kernel_port PUBLIC freertos_kernel_port_headers
    PRIVATE freertos_config amebagreen2_sdk)
target_include_directories(freertos_kernel_port PRIVATE
    ${AT_ROOT}/component/freertos-kernel_config/freertos-kernel/include)
target_link_libraries(freertos_config PUBLIC freertos_kernel_port_headers amebagreen2_sdk)

target_include_directories(freertos_config PUBLIC ${AT_ROOT}/component/freertos-kernel_config/freertos-kernel/include)
