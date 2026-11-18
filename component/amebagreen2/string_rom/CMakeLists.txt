# string_rom: local AmebaGreen2 files, grouped like component/amebad.
add_library(amebagreen2_string_rom_headers INTERFACE)
target_include_directories(amebagreen2_string_rom_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
