include_guard()

# Wires component/mbedtls (Mbed TLS v3.6.4, clean upstream) into the
# chip-toolchain build.
#
# Why v3.6.4 and not v3.6.5+: the 3.6.5 release reorganized the source
# tree (most primitives moved under tf-psa-crypto/drivers/builtin/src/)
# AND removed the classic MBEDTLS_*_ALT mechanism for the in-tree builtin
# drivers. Bouffalo's HW-accelerated alt impls (aes_alt.c, sha*_alt.c,
# bignum_alt.c, ecp_alt.c, …) rely on MBEDTLS_*_ALT to substitute the
# software implementations at compile time. Pinning at 3.6.4 keeps that
# contract intact while still being a supported LTS release.
#
# Targets produced (by upstream's CMakeLists, all STATIC since we force
# BUILD_SHARED_LIBS=OFF; EXCLUDE_FROM_ALL keeps them out of `all` until
# something pulls them in):
#   mbedcrypto    crypto primitives + PSA + DRBG
#   mbedx509      X.509 cert handling
#   mbedtls       TLS protocol (depends on the above)
#   everest       Curve25519 fast impl (3rdparty submodule)
#   p256m         secp256r1 fast impl (3rdparty submodule)
#
# Required environment:
#   AT_PROJECT_ROOT set.
#   bflb_mbedtls_config and bflb_mbedtls_port already add_subdirectory'd
#     (the config target provides MBEDTLS_CONFIG_FILE; port provides
#     platform glue + entropy_alt + timing_alt + net_sockets that the
#     upstream library refers to weakly).

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/mbedtls.cmake")
endif()

# bflb_mbedtls_config / port / hw_acc all live inside bouffalo_sdk; their
# headers (MBEDTLS_CONFIG_FILE, platform glue) feed into upstream
# mbedcrypto compilation. Pull via the glue file — include_guard() makes
# this idempotent so any call order works.
include(${AT_PROJECT_ROOT}/cmake/bouffalo_sdk.cmake)

# Strip upstream's testing scaffolding and install/export — none of it
# makes sense on bare metal.
set(ENABLE_TESTING       OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS      OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON  CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(GEN_FILES            OFF CACHE BOOL "" FORCE)

# Same compile-test workaround as zlib / cjson — cross toolchains can't
# link a bare hello-world.
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

# Point upstream at the Bouffalo config wrapper (config-tls-generic.h)
# carried by bflb_mbedtls_config. Use -DMBEDTLS_CONFIG_FILE +
# include_directories BEFORE the subdirectory so every target the
# upstream tree creates (mbedcrypto, mbedtls, mbedx509, p256m, everest,
# …) sees both the macro AND the path that makes it resolve.
add_compile_definitions(MBEDTLS_CONFIG_FILE="config-tls-generic.h")
include_directories("${AT_PROJECT_ROOT}/component/bouffalo_sdk/bflb_mbedtls_config")
# hw_acc headers (aes_alt.h etc.) likewise need to be on the include
# path of every TU because upstream's public headers #include "aes_alt.h"
# unconditionally when MBEDTLS_AES_ALT is set.
include_directories("${AT_PROJECT_ROOT}/component/bouffalo_sdk/bflb_mbedtls_hw_acc")
# Same story for the port header named by MBEDTLS_PLATFORM_STD_MEM_HDR.
include_directories("${AT_PROJECT_ROOT}/component/bouffalo_sdk/bflb_mbedtls_port")

add_subdirectory(${AT_PROJECT_ROOT}/component/mbedtls
                 ${CMAKE_BINARY_DIR}/at_mbedtls_build
                 EXCLUDE_FROM_ALL)

# Wire the Bouffalo include paths (config + hw_acc) onto the upstream
# targets so the config file resolves and the alt headers are visible
# when MBEDTLS_*_ALT macros are on.
#
# bflb_mbedtls_port is NOT linked here — that would create a cycle
# (port links mbedcrypto for upstream headers; doing the reverse would
# break configuration). Instead we expose only port's source directory
# to mbedcrypto (header edge, not symbol edge), so
# MBEDTLS_PLATFORM_STD_MEM_HDR="mbedtls_port_bouffalo_sdk.h" resolves
# during upstream compilation. Using
# $<TARGET_PROPERTY:bflb_mbedtls_port,SOURCE_DIR> rather than its
# INTERFACE_INCLUDE_DIRECTORIES is deliberate: port itself reads
# mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES (for the mbedtls/ public
# headers), and feeding port's INTERFACE includes back into mbedcrypto
# would close a generator-expression cycle. SOURCE_DIR is a fixed
# string, so the propagation stays one-way.
foreach(_t mbedcrypto mbedtls mbedx509)
    if(TARGET ${_t})
        if(TARGET bflb_mbedtls_config)
            target_link_libraries(${_t} PUBLIC bflb_mbedtls_config)
        endif()
        # hw_acc and port both expose mbedtls/* headers transitively via
        # $<TARGET_PROPERTY:mbedcrypto,INTERFACE_INCLUDE_DIRECTORIES>.
        # PUBLIC-linking them into mbedcrypto would close the
        # generator-expression cycle (mbedcrypto's INTERFACE evaluation
        # would pull theirs, which references mbedcrypto's again).
        # Propagate only the SOURCE_DIR (fixed string) so mbedcrypto's
        # compile still sees aes_alt.h / mbedtls_port_bouffalo_sdk.h
        # without re-entering the cycle. Symbol-level linkage of hw_acc
        # comes from the bouffalo_sdk INTERFACE_LINK_LIBRARIES list
        # below.
        if(TARGET bflb_mbedtls_hw_acc)
            target_include_directories(${_t} PUBLIC
                "$<TARGET_PROPERTY:bflb_mbedtls_hw_acc,SOURCE_DIR>"
            )
        endif()
        if(TARGET bflb_mbedtls_port)
            target_include_directories(${_t} PUBLIC
                "$<TARGET_PROPERTY:bflb_mbedtls_port,SOURCE_DIR>"
            )
        endif()
        # Silence upstream warnings we can't reasonably fix.
        target_compile_options(${_t} PRIVATE -w)
    endif()
endforeach()

# Stitch into the bouffalo_sdk umbrella so downstream just links
# bouffalo_sdk and gets the TLS stack transitively. Ameba projects don't
# define bouffalo_sdk, hence the guard.
if(TARGET bouffalo_sdk)
    # hw_acc + port both go on the umbrella INTERFACE link list so the
    # final ELF picks up their symbols. mbedcrypto used to PUBLIC-link
    # hw_acc above, but that closed an include-cycle once hw_acc started
    # pulling mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES — wiring it
    # through bouffalo_sdk keeps symbol resolution intact without the
    # cycle. (bflb_mbedtls_port is already stitched in by
    # component/bouffalo_sdk/CMakeLists.txt's port-adapter block.)
    target_link_libraries(bouffalo_sdk INTERFACE
        mbedtls mbedcrypto mbedx509
        bflb_mbedtls_hw_acc
    )
endif()
