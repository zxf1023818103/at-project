# mbed: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_mbed_headers INTERFACE)
target_include_directories(amebadplus_mbed_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
