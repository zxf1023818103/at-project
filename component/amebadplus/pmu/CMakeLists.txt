# pmu: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_pmu_headers INTERFACE)
target_include_directories(amebadplus_pmu_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

add_library(amebadplus_pmc STATIC IMPORTED GLOBAL)
set_target_properties(amebadplus_pmc PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_pmc_${AMEBADPLUS_CORE}.a")
