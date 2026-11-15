# freertos_tcp_port: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_freertos_tcp_port_headers INTERFACE)
target_include_directories(amebadplus_freertos_tcp_port_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/lwip_compat")
