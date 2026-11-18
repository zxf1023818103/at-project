# pmu: local AmebaGreen2 files, grouped like component/amebad.
add_library(amebagreen2_pmu_headers INTERFACE)
target_include_directories(amebagreen2_pmu_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

add_library(amebagreen2_pmc STATIC IMPORTED GLOBAL)
set_target_properties(amebagreen2_pmc PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_pmc_${AMEBAGREEN2_CORE}.a")

if(AMEBAGREEN2_CORE STREQUAL "km4tz")
    # The vendor archive logs every AP clock-gate/wake at ALWAYS level.
    target_sources(amebagreen2_pmc INTERFACE "${CMAKE_CURRENT_LIST_DIR}/pmc_log_filter.S")
    target_link_options(amebagreen2_pmc INTERFACE -Wl,--wrap=rtk_log_write_nano)
endif()
