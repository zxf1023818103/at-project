# freertos-plus-tcp_config — home for the freertos-plus-tcp submodule (component/freertos-plus-tcp_config/freertos-plus-tcp).
#
# No target is produced here: freertos-plus-tcp is consumer-coupled (it needs build config the
# consumer supplies — config header / port adapters / FREERTOS_PORT / etc.), so it is
# add_subdirectory'd (or its sources/includes are pulled) directly by component/freertos_tcp_config,
# pointing at component/freertos-plus-tcp_config/freertos-plus-tcp. This file exists only to give the config
# component a CMakeLists; it is intentionally not add_subdirectory'd from anywhere.
