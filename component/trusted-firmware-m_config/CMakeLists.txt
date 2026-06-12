# trusted-firmware-m_config — home for the trusted-firmware-m submodule (component/trusted-firmware-m_config/trusted-firmware-m).
#
# No target is produced here: trusted-firmware-m is consumer-coupled (it needs build config the
# consumer supplies — config header / port adapters / FREERTOS_PORT / etc.), so it is
# add_subdirectory'd (or its sources/includes are pulled) directly by component/amebad/app_km4 + app_km4_secure (TF-M secure world),
# pointing at component/trusted-firmware-m_config/trusted-firmware-m. This file exists only to give the config
# component a CMakeLists; it is intentionally not add_subdirectory'd from anywhere.
