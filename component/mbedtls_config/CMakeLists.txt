# component/mbedtls_config — home for the Mbed TLS v3.6.4 submodule
# (component/mbedtls_config/mbedtls), built via its OWN upstream CMake (add_subdirectory).
#
# Consumer-coupled: the upstream sources are unbuildable without a config
# (MBEDTLS_CONFIG_FILE) + the Bouffalo port/hw_acc adapter headers, and only the Bouffalo
# SDK consumes upstream mbedtls (AmebaD uses ROM mbedtls — headers only, no build, via
# component/amebad/mbedtls_rom). So this component is add_subdirectory'd by
# component/bouffalo_sdk, which FIRST sets the directory-scoped MBEDTLS_CONFIG_FILE +
# the bflb config/port/hw_acc include dirs (inherited into this scope); this file sets the
# platform-agnostic upstream option knobs and add_subdirectory's the submodule.
#
# Targets produced (all STATIC — BUILD_SHARED_LIBS forced OFF): mbedcrypto / mbedx509 /
# mbedtls (+ everest / p256m). The consumer wires the bflb adapters onto them afterward.
#
# Pinned at v3.6.4 (not 3.6.5+) so the classic MBEDTLS_*_ALT mechanism Bouffalo's
# HW-accelerated impls rely on stays intact — see component/bouffalo_sdk for the why.

# Strip upstream's testing scaffolding + install/export (meaningless on bare metal).
set(ENABLE_TESTING       OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS      OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON  CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(GEN_FILES            OFF CACHE BOOL "" FORCE)

add_subdirectory(mbedtls "${CMAKE_BINARY_DIR}/at_mbedtls_build" EXCLUDE_FROM_ALL)
