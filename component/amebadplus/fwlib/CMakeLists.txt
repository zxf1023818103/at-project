# fwlib: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_fwlib_headers INTERFACE)
target_include_directories(amebadplus_fwlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

add_library(amebadplus_chipinfo STATIC IMPORTED GLOBAL)
set_target_properties(amebadplus_chipinfo PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_chipinfo_${AMEBADPLUS_CORE}.a")
