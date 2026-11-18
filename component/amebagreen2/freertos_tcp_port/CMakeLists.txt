# freertos_tcp_port: local AmebaGreen2 files, grouped like component/amebad.
add_library(amebagreen2_freertos_tcp_port_headers INTERFACE)
target_include_directories(amebagreen2_freertos_tcp_port_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/lwip_compat")
