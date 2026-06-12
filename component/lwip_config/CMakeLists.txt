# lwip_config — home for the lwip submodule (component/lwip_config/lwip).
#
# No target is produced here: lwip is consumer-coupled (it needs build config the
# consumer supplies — config header / port adapters / FREERTOS_PORT / etc.), so it is
# add_subdirectory'd (or its sources/includes are pulled) directly by bouffalo_sdk/bflb_lwip_port,
# pointing at component/lwip_config/lwip. This file exists only to give the config
# component a CMakeLists; it is intentionally not add_subdirectory'd from anywhere.
