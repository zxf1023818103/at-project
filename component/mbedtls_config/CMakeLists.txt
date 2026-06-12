# mbedtls_config — home for the mbedtls submodule (component/mbedtls_config/mbedtls).
#
# No target is produced here: mbedtls is consumer-coupled (it needs build config the
# consumer supplies — config header / port adapters / FREERTOS_PORT / etc.), so it is
# add_subdirectory'd (or its sources/includes are pulled) directly by bouffalo_sdk (Mbed TLS lib + bflb_mbedtls_* port adapters) and amebad/mbedtls_rom,
# pointing at component/mbedtls_config/mbedtls. This file exists only to give the config
# component a CMakeLists; it is intentionally not add_subdirectory'd from anywhere.
