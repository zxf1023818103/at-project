# fwlib: local AmebaGreen2 files, grouped like component/amebad.
add_library(amebagreen2_fwlib_headers INTERFACE)
target_include_directories(amebagreen2_fwlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

add_library(amebagreen2_chipinfo STATIC IMPORTED GLOBAL)
set_target_properties(amebagreen2_chipinfo PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_chipinfo_${AMEBAGREEN2_CORE}.a")
