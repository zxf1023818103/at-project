# component/mbedtls_config — home for the Mbed TLS v3.6.4 submodule
# (component/mbedtls_config/mbedtls), built via its OWN upstream CMake (add_subdirectory).
#
# Platform-agnostic: this file only sets the upstream option knobs, add_subdirectory's the
# submodule, and builds the shared FreeRTOS threading-alt glue. ALL per-platform mbedTLS config
# (the MBEDTLS_USER_CONFIG_FILE deltas, the MBEDTLS_*_ALT selections, include wiring) lives in
# each platform's mbedtls_port (component/amebad/mbedtls_port, component/bouffalo_sdk/mbedtls_port),
# which attaches it onto the upstream targets there. Both AmebaD and Bouffalo build upstream from
# here on the upstream DEFAULT config + their own user config (no MBEDTLS_CONFIG_FILE replacement).
#
# Targets produced (all STATIC — BUILD_SHARED_LIBS forced OFF): mbedcrypto / mbedx509 /
# mbedtls (+ everest / p256m). The consumer's mbedtls_port wires its config onto them afterward.
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
# Export the dir so a port can reference threading_alt.h without a fragile relative path.
set(MBEDTLS_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL
    "component/mbedtls_config source dir (threading_alt.h / mbedtls_threading_alt.c)")

# NO platform config here. All per-platform mbedTLS config (the MBEDTLS_USER_CONFIG_FILE deltas,
# the MBEDTLS_*_ALT selections, and the include wiring) lives in each platform's mbedtls_port
# (component/amebad/mbedtls_port, component/bouffalo_sdk/mbedtls_port), which attaches it onto the
# upstream targets itself. This component only builds the vanilla upstream submodule + the shared
# FreeRTOS threading-alt glue below.

# `mbedtls_config` — the umbrella consumers link to pull the WHOLE upstream mbedTLS stack with one
# target. It compiles the FreeRTOS threading-alt and re-exports the three upstream archives, so a
# consumer just `target_link_libraries(<x> mbedtls_config)` and gets crypto + x509 + TLS without
# naming each. Not chip-gated — every dep is a platform-agnostic target (AmebaD + Bouffalo both
# build them and attach their own mbedTLS config PUBLIC onto crypto/x509/tls):
#   - mbedcrypto / mbedx509 / mbedtls : the upstream libs (primitives+PSA / X.509 / TLS handshake).
#     Re-exported PUBLIC. There's no --gc-sections here, but static-archive selection is per-member
#     so an image that calls no TLS symbols pulls no mbedtls/x509 members — TLS-less images stay
#     small. Linking these also INHERITS their config surface (MBEDTLS_THREADING_ALT + the
#     threading_alt.h include path) so threading_alt.c's mutex types match the rest of the build.
#   - FreeRTOS / freertos_config : the FreeRTOS.h / semphr.h the callbacks are written against
#     (shared targets from component/freertos-kernel_config — same name on every platform).
# All resolve as forward references at generate time. EXCLUDE_FROM_ALL: built only when linked.
# NB AmebaD additionally attaches its HW-ALT/entropy `mbedtls_port` onto this umbrella from
# component/amebad/mbedtls_port (a platform-specific augmentation kept out of this shared file).
# Header-only view of the shared mbedTLS config (mbedtls_user_config.h / mbedtls_threading_alt.h) for
# the per-platform mbedtls_cfg configs that -include them WITHOUT linking the mbedtls_config archive —
# which would pull mbedcrypto and cycle back through the config. Link this instead of a raw path.
add_library(mbedtls_config_headers INTERFACE)
target_include_directories(mbedtls_config_headers SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(mbedtls_config STATIC EXCLUDE_FROM_ALL mbedtls_threading_alt.c)
target_link_libraries(mbedtls_config mbedcrypto mbedx509 mbedtls FreeRTOS freertos_config)
