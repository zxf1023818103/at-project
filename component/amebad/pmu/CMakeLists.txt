# amebad pmu — Realtek PMU / power management module: the API headers
# (freertos_pmu.h + rtl8721d_freertos_pmu.h) and the prebuilt PMC/SOCPS
# implementation libs (lib_pmc_lp.a KM0 / lib_pmc_hp.a KM4 — binary blobs, no
# source). nm confirms the libs define the pmu_* / freertos_*_sleep_processing
# symbols the header declares.
#
# This file owns the prebuilt IMPORTED lib (chip-gated). The headers are also
# broadly needed (ameba_soc.h pulls freertos_pmu.h into every TU), so they stay
# exposed via amebad_sdk_includes (which points at this dir); the IMPORTED lib
# also carries this dir on its INTERFACE includes so a direct consumer gets them.
#
# Resync the .a from project_{lp,hp}/asdk/lib/application when the
# platform/ameba-rtos-d submodule pointer moves.

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_pmc amebad_pmc_lp)
    set(_lib lib_pmc_lp.a)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_pmc amebad_pmc_hp)
    set(_lib lib_pmc_hp.a)
else()
    return()
endif()

# GLOBAL so the sibling app_km0 / app_km4 dirs can link it.
add_library(${_pmc} STATIC IMPORTED GLOBAL)
set_target_properties(${_pmc} PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/${_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
