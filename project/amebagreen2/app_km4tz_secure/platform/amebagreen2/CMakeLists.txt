cmake_policy(SET CMP0076 NEW)
set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(platform_region_defs INTERFACE partition)
target_compile_options(platform_s PUBLIC ${COMPILER_CMSE_FLAG})
target_include_directories(platform_s PUBLIC . partition cmsis_core cmsis_drivers
    ${CMAKE_SOURCE_DIR}/interface/include)
target_sources(platform_s PRIVATE target_cfg.c tfm_hal_platform.c tfm_hal_isolation.c
    cmsis_core/system_core_init.c cmsis_drivers/Driver_USART.c services/src/tfm_platform_system.c
    services/src/provisioning.c cmsis_drivers/Driver_Flash.c)
target_sources(tfm_s PRIVATE cmsis_core/startup_amebagreen2.c)
get_filename_component(_root ${CMAKE_CURRENT_LIST_DIR}/../../../../.. ABSOLUTE)
target_sources(platform_s PRIVATE ${_root}/component/amebagreen2/ota_flash_port/image_verify.c)
target_compile_definitions(platform_s PRIVATE GREEN2_IMAGE_SECURE)
target_link_options(tfm_s PRIVATE -Wl,--wrap=tfm_core_panic
    -T${_root}/project/amebagreen2/bootloader_km4tz/ameba_rom_symbol_bcut_s.ld)
if(GREEN2_EFUSE_DEBUG)
    get_filename_component(_root ${CMAKE_CURRENT_LIST_DIR}/../../../../.. ABSOLUTE)
    target_include_directories(platform_s PRIVATE ${_root}/component/amebagreen2/efuse_port)
    target_sources(platform_s PRIVATE ${_root}/component/amebagreen2/efuse_port/efuse_debug.c)
    target_compile_definitions(platform_s PRIVATE GREEN2_EFUSE_DEBUG GREEN2_EFUSE_SECURE)
endif()
if(GREEN2_TFM_STORAGE)
    target_sources(platform_s PRIVATE services/src/entropy.c)
endif()
# An empty sgstubs_end section does not advance GNU ld's FLASH allocation
# cursor. Materialize its padding so ordinary secure code cannot occupy NSC.
set(_ld_source ${PLATFORM_DIR}/ext/common/gcc/tfm_common_s.ld)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_ld_source})
file(READ ${_ld_source} _ld)
string(REPLACE ".sgstubs_end : TFM_LINKER_VENEERS_END \\\n{ \\\n} > FLASH"
    ".sgstubs_end : \\\n{ \\\n    . = ALIGN(CMSE_VENEER_REGION_SIZE); \\\n} > FLASH" _ld "${_ld}")
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/green2_tfm_s.ld CONTENT "${_ld}" @ONLY)
target_add_scatter_file(tfm_s $<$<C_COMPILER_ID:GNU>:${CMAKE_CURRENT_BINARY_DIR}/green2_tfm_s.ld>)
if(TARGET platform_ns)
    target_include_directories(platform_ns PUBLIC . partition cmsis_core)
endif()
