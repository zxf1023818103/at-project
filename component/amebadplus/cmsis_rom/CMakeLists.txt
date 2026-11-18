# cmsis_rom: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_cmsis_rom_headers INTERFACE)
target_include_directories(amebadplus_cmsis_rom_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
