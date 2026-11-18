# monitor_port: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_monitor_port_headers INTERFACE)
target_include_directories(amebadplus_monitor_port_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

add_library(amebadplus_crashdump STATIC IMPORTED GLOBAL)
set_target_properties(amebadplus_crashdump PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_crashdump_${AMEBADPLUS_CORE}.a")
