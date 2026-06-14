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

# Shared (non-vendored) mbedTLS glue that also lives here, for platform ports to consume:
#   - threading_alt.h               : the FreeRTOS-friendly opaque mutex type (void*)
#   - mbedtls_threading_alt.c       : the MBEDTLS_THREADING_ALT FreeRTOS callbacks +
#                                     mbedtls_threading_freertos_init()
#   - mbedtls_amebad_user_config.h  : the AmebaD MBEDTLS_USER_CONFIG_FILE deltas
# Export the dir so a port can reference these headers (user config / threading_alt.h) without a
# fragile relative path.
set(MBEDTLS_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL
    "component/mbedtls_config source dir (threading_alt.h / mbedtls_threading_alt.c / user config)")

# AmebaD: compile the FreeRTOS threading-alt into its own `mbedtls_config` lib. It links mbedtls +
# mbedtls_port (both carry amebad_mbedtls_cfg PUBLIC) only to INHERIT the config surface
# (MBEDTLS_THREADING_ALT + the include path to threading_alt.h) so the mutex types match the rest
# of the build. `mbedtls_port` is a forward reference — defined later in the amebad subtree, which
# is add_subdirectory'd after this dir; target_link_libraries resolves target names at generate
# time, so that's fine. Bouffalo also add_subdirectory's this component but has no mbedtls_port
# (and its own threading), so this is gated on the AmebaD chip.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(mbedtls_config STATIC EXCLUDE_FROM_ALL mbedtls_threading_alt.c)
    # Link mbedcrypto (not the TLS `mbedtls` lib) + mbedtls_port purely to INHERIT their config:
    # both carry amebad_mbedtls_cfg PUBLIC, which is all threading_alt.c needs to compile
    # (MBEDTLS_THREADING_ALT + the threading_alt.h include path). mbedcrypto is the archive the
    # consumers' cross_refs LINK_GROUP already pulls; linking the TLS `mbedtls` lib instead pulls a
    # group-external target and forms a strongly-connected cycle (the group depends on it and it
    # depends back into the group), which the generator rejects.
    target_link_libraries(mbedtls_config mbedcrypto mbedtls_port)
endif()
