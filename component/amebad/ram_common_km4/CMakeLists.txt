# amebad_ram_common_km4 — the subset of the shared HAL (ram_common) that the
# KM4 image2 needs but the HP ROM does NOT provide:
#   rtl8721d_ipc_api.c     ipc_send_message / ipc_table_init
#   rtl8721d_gdma_memcpy.c memcpy_gdma_init
#   rtl8721d_gdma_ram.c    GDMA_ChnlAlloc / GDMA HAL
#   rtl8721d_flash_ram.c   FLASH_Write_Lock / _Unlock / _IPC_Int
#   rtl8721d_ram_libc.c    _rtl_printf and friends
#
# Surgical, NOT all of ram_common: the rest of the shared HAL comes from the HP
# ROM (rom_symbol table), so compiling those files too would DUPLICATE ROM
# symbols. (And ram_common as a whole is KM0-only anyway — rtl8721d_qdec.c uses
# the LP-only RCC header.) These files don't pull any LP-only header and compile
# under KM4. Reuses the mirrored copies under ../ram_common/src/. KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_src "${CMAKE_CURRENT_SOURCE_DIR}/../ram_common/src")

set(_files
    rtl8721d_ipc_api.c
    rtl8721d_gdma_memcpy.c
    rtl8721d_gdma_ram.c
    rtl8721d_flash_ram.c
    rtl8721d_ram_libc.c
)
set(_abs)
foreach(_p ${_files})
    if(NOT EXISTS "${_src}/${_p}")
        message(FATAL_ERROR "amebad_ram_common_km4: missing '${_src}/${_p}'")
    endif()
    list(APPEND _abs "${_src}/${_p}")
endforeach()

add_library(amebad_ram_common_km4 STATIC ${_abs})

target_link_libraries(amebad_ram_common_km4 PUBLIC amebad_sdk_includes)

target_compile_options(amebad_ram_common_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
