add_library(ota_flash_port STATIC ota_flash_dplus.c ota_platform.c
    ${AT_ROOT}/component/ota_flash_config/ota_lock.c)
if(NOT AT_ENABLE_TFM)
    target_sources(ota_flash_port PRIVATE image_verify.c)
endif()
target_link_libraries(ota_flash_port PUBLIC ota_flash_config PRIVATE amebadplus_sdk FreeRTOS board_flash_lock)
