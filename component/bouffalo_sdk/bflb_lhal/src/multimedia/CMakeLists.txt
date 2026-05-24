# bflb_lhal_multimedia — built from source when src/ exists, otherwise from a
# prebuilt static archive shipped under lib/. Exposes a single target name
# (bflb_lhal_multimedia) regardless of which branch fires, so the parent can
# unconditionally target_link_libraries against it.

if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_library(bflb_lhal_multimedia STATIC)
    target_include_directories(bflb_lhal_multimedia PRIVATE include)
    if("${BOUFFALO_CHIP}" STREQUAL "bl616cl")
        target_sources(bflb_lhal_multimedia PRIVATE src/bflb_mjdec_v2.c)
    endif()
else()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/libmultimedia_${BOUFFALO_CHIP}.a")
        message(FATAL_ERROR
            "no libmultimedia_${BOUFFALO_CHIP}.a found under "
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/")
    endif()
    add_library(bflb_lhal_multimedia STATIC IMPORTED GLOBAL)
    set_target_properties(bflb_lhal_multimedia PROPERTIES
        IMPORTED_LOCATION
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/libmultimedia_${BOUFFALO_CHIP}.a"
    )
endif()
