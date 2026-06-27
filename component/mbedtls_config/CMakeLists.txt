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

# These selections live HERE, each next to the file it names/uses (tree-structured: a macro sits
# with its implementation, not in a far-off port):
#   - MBEDTLS_USER_CONFIG_FILE -> mbedtls_amebad_user_config.h (this dir): the default-config deltas
#     (disable net/fs/time, enable threading + no-platform-entropy).
#   - MBEDTLS_THREADING_ALT    -> threading_alt.h + mbedtls_threading_alt.c (this dir): route the
#     mbedTLS mutexes to the FreeRTOS callbacks compiled into the mbedtls_config lib above.
# Attached PUBLIC onto the upstream targets so (a) their own sources compile with them and (b) every
# consumer inherits them; the include dir (this dir) lets their threading.h find threading_alt.h.
# AmebaD-only: Bouffalo replaces the whole config via MBEDTLS_CONFIG_FILE=config-tls-generic.h (set
# in component/bouffalo_sdk) and doesn't use the FreeRTOS threading-alt, so neither may leak there —
# upstream threading.h pulls threading_alt.h on MBEDTLS_THREADING_ALT alone (no THREADING_C guard).
# The HW-specific *_ALT (entropy/gcm/chachapoly) sit with THEIR impls in component/amebad/mbedtls_port.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    foreach(_t mbedcrypto mbedx509 mbedtls p256m everest)
        if(TARGET ${_t})
            target_compile_definitions(${_t} PUBLIC
                MBEDTLS_USER_CONFIG_FILE="mbedtls_amebad_user_config.h"
                MBEDTLS_THREADING_ALT)
            target_include_directories(${_t} SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
            # MBEDTLS_FS_IO is on (user config) -> x509_crt.c #include <dirent.h>, which newlib
            # ships as a `#error` stub. component/syscall_config supplies a real <dirent.h> (+ the
            # opendir/readdir defs); put its dir on the include path AHEAD of the sysroot so the
            # mbedtls FS_IO sources (parse_file/parse_path) compile on this bare-metal target.
            target_include_directories(${_t} BEFORE PRIVATE
                "${AT_ROOT}/component/syscall_config")
        endif()
    endforeach()
endif()

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
add_library(mbedtls_config STATIC EXCLUDE_FROM_ALL mbedtls_threading_alt.c)
target_link_libraries(mbedtls_config mbedcrypto mbedx509 mbedtls FreeRTOS freertos_config)
