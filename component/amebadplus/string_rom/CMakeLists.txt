# string_rom: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_string_rom_headers INTERFACE)
target_include_directories(amebadplus_string_rom_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
