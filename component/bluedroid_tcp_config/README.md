# Standalone Bluedroid TCP build adapter

Used only by `project/bl618dg_pan`, which supplies `freertos_config` and
FreeRTOSIPConfig.h. Reuses the existing FreeRTOS+TCP source submodule at
`../freertos-plus-tcp_config/freertos-plus-tcp`, without changing the shared
Ameba/Bouffalo TCP configuration or global component registration.

This adapter belongs to a standalone SDK CMake project. Do not add it to the
global component tree together with the shared TCP configuration: both
intentionally supply the same upstream stack targets.
