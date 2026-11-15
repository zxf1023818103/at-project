# usrcfg: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_usrcfg_headers INTERFACE)
target_include_directories(amebadplus_usrcfg_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
