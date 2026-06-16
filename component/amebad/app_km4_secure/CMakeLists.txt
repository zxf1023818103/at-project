# component/amebad/app_km4_secure — AmebaD RTL8721D KM4 secure world = TF-M (SPE).
#
# TF-M is a standalone CMake project (its own toolchain file, runs Python manifest
# tooling at configure, assumes it is the top-level CMAKE_SOURCE_DIR) — it cannot
# be add_subdirectory'd into the km4 tree. So we build it with ExternalProject,
# exactly the model TF-M ships for downstream integration in cmake/spe-CMakeLists.
# cmake (the template tf-m-tests/tests_reg/spe uses).
#
# The build configures TF-M against our out-of-tree platform port
# (platform/rtl8721d), builds the SPE, and `install`s the NS-facing export
# (s_veneers.o + PSA client interface/headers + generated headers, and the
# spe-CMakeLists template renamed to the export's CMakeLists.txt) into
# tfm_spe_export/. The NS side (app_km4) consumes that export by path — see
# component/amebad/app_km4/CMakeLists.txt (single km4 build tree => can't
# add_subdirectory the export, which only exists after this build step).
#
# Output: ${CMAKE_CURRENT_BINARY_DIR}/tfm_s.bin (copied from TF-M's bin/), which
# build_image_all.py lays at flash 0x08100000. Build-dir only; nothing is written
# to the source tree.
#
# Opt-in: -DAMEBAD_TFM_BOOT=ON (make TFM=ON). KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

if(NOT AMEBAD_TFM_BOOT)
    return()
endif()

include(ExternalProject)

# ---- Inputs (all absolute) ------------------------------------------------
set(_tfm_src       "${CMAKE_SOURCE_DIR}/component/trusted-firmware-m_config/trusted-firmware-m")
set(_tfm_platform  "${CMAKE_CURRENT_SOURCE_DIR}/platform/rtl8721d")
set(_tfm_toolchain "${_tfm_src}/toolchain_GNUARM.cmake")

# arm-none-eabi toolchain bin dir — TF-M's toolchain_GNUARM.cmake resolves the
# compiler via find_program(${CROSS_COMPILE}-gcc), which searches PATH. The km4
# build's compiler is not on PATH, so derive its dir from CMAKE_C_COMPILER (tracks
# AMEBAD_TOOLCHAIN_ROOT) and inject it on PATH for the nested configure/build.
get_filename_component(_tc_bin "${CMAKE_C_COMPILER}" DIRECTORY)

# Python with TF-M's manifest-tool deps (cbor2/jinja2/yaml/click/jsonschema).
if(NOT DEFINED AMEBAD_TFM_PYTHON)
    set(AMEBAD_TFM_PYTHON "${CMAKE_SOURCE_DIR}/.venv-tfm/bin/python3"
        CACHE FILEPATH "Python interpreter for the TF-M manifest tooling")
endif()

set(_tfm_bindir "${CMAKE_CURRENT_BINARY_DIR}/tfm_spe")
set(_tfm_export "${CMAKE_CURRENT_BINARY_DIR}/tfm_spe_export")
set(_tfm_s_bin  "${_tfm_bindir}/bin/tfm_s.bin")

set(_tfm_build_type "${CMAKE_BUILD_TYPE}")
if(NOT _tfm_build_type)
    set(_tfm_build_type MinSizeRel)
endif()

set(_tfm_args
    "-DTFM_PLATFORM=${_tfm_platform}"
    "-DTFM_TOOLCHAIN_FILE=${_tfm_toolchain}"
    "-DCROSS_COMPILE=arm-none-eabi"
    "-DCMAKE_PROGRAM_PATH=${_tc_bin}"
    "-DPython3_EXECUTABLE=${AMEBAD_TFM_PYTHON}"
    "-DBL2=OFF"
    "-DCMAKE_INSTALL_PREFIX=${_tfm_export}"
    "-DCMAKE_BUILD_TYPE=${_tfm_build_type}"
    # Share ONE Mbed TLS version with the NS side. TF-M's lib/ext/mbedcrypto must own/patch
    # its own checkout (apply_patches does a destructive git reset --hard / clean -df on the
    # source), so we can't point it AT our submodule. Instead, make it FETCH FROM our local
    # tree at OUR version (mbedtls-3.6.4) — no separate upstream download, same version on
    # SPE + NS, and our submodule stays untouched (TF-M clones into its own build dir).
    # The CC3XX patch (0005) is renamed out (above): it doesn't apply to 3.6.4 and the
    # RTL8721D has no CryptoCell (CRYPTO_HW_ACCELERATOR=OFF -> vanilla software wrappers).
    "-DMBEDCRYPTO_GIT_REMOTE=${CMAKE_SOURCE_DIR}/component/mbedtls_config/mbedtls"
    "-DMBEDCRYPTO_VERSION=mbedtls-3.6.4"
)

# PSA secure storage (make STORAGE=ON): enables ITS + PS (encrypted) + the Crypto
# partition (mbedcrypto) + NV counters in the SPE. The actual partition/crypto/OTP
# knobs are gated on AMEBAD_TFM_STORAGE inside platform/rtl8721d/config.cmake; here
# we just forward the flag into the TF-M configure. Backed by the real flash driver
# (cmsis_drivers/Driver_Flash.c) + the flash_layout.h TFM_HAL_ITS/PS_* + OTP/NV areas.
if(AMEBAD_TFM_STORAGE)
    list(APPEND _tfm_args "-DAMEBAD_TFM_STORAGE=ON")
endif()

# Skip TF-M's CC3XX mbedcrypto patch (0005): it targets mbedtls-3.6.2 and does NOT apply to
# our shared 3.6.4, and the RTL8721D has no CryptoCell (CRYPTO_HW_ACCELERATOR=OFF -> vanilla
# software PSA driver wrappers), so it is unneeded. Renaming it out of the *.patch glob that
# fetch_remote_library applies is done HERE (from our tracked CMake) so it is reproducible
# after a `git submodule update` restores the upstream TF-M tree.
set(_cc3xx_patch "${_tfm_src}/lib/ext/mbedcrypto/0005-Hardcode-CC3XX-entry-points.patch")
if(EXISTS "${_cc3xx_patch}")
    file(RENAME "${_cc3xx_patch}" "${_cc3xx_patch}.skip-rtl8721d-sw")
endif()

# RTL8721D: override TF-M's default NV-seed write. mbedTLS rewrites the CRYPTO_NV_SEED in ITS on
# every boot (forward-secrecy seed update); that write triggers an ITS block GC which copies any
# large co-located object (a provisioned AWS key/cert) as long 256-byte SPIC page programs, whose
# wide user-mode window corrupts KM0's concurrent XIP fetches -> KM0 boot HardFault (AN0400 §18.1).
# Our version persists only the FIRST (provisioning) write and skips the per-boot update. Copied
# from our tracked source over the upstream template at configure time so it survives a
# `git submodule update` (same reproducible pattern as the CC3XX rename above). See the
# project_amebad_aws_iot memory + doc/amebad_km0_scheduler_and_runtime_flash.md.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/rtl8721d/crypto_nv_seed.c"
    "${_tfm_src}/platform/ext/common/template/crypto_nv_seed.c"
    COPYONLY)

# Wrap the nested CMake invocations so the toolchain bin dir is on PATH.
set(_env "${CMAKE_COMMAND}" -E env "PATH=${_tc_bin}:$ENV{PATH}")

ExternalProject_Add(tfm_spe
    SOURCE_DIR  "${_tfm_src}"
    BINARY_DIR  "${_tfm_bindir}"
    CONFIGURE_COMMAND
        ${_env} "${CMAKE_COMMAND}" -G "Unix Makefiles"
                -S "${_tfm_src}" -B "${_tfm_bindir}" ${_tfm_args}
    BUILD_COMMAND
        ${_env} "${CMAKE_COMMAND}" --build "${_tfm_bindir}"
    INSTALL_COMMAND
        ${_env} "${CMAKE_COMMAND}" --build "${_tfm_bindir}" --target install
    BUILD_ALWAYS  TRUE          # descend every build so port/SPE edits rebuild
    BUILD_BYPRODUCTS
        "${_tfm_s_bin}"
        "${_tfm_export}/interface/lib/s_veneers.o"
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD     TRUE
    USES_TERMINAL_INSTALL   TRUE
)

# Copy tfm_s.bin out of TF-M's bin/ to where build_image_all.py reads it
# (build dir only — never the source tree).
ExternalProject_Add_Step(tfm_spe copy_blob
    DEPENDEES install
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_tfm_s_bin}" "${CMAKE_CURRENT_BINARY_DIR}/tfm_s.bin"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/tfm_s.bin"
    ALWAYS TRUE
    COMMENT "amebad: copy TF-M SPE -> tfm_s.bin (build dir)"
)

# NB: the platform port (platform/rtl8721d) is built INSIDE the TF-M build above
# (TFM_PLATFORM). It already does target_sources(platform_s/tfm_s ...) — that is
# the injection point for sharing in-tree source into the SPE later (a shared
# *_sources.cmake the port `include()`s), without a second add_subdirectory.
#
# The real NSPE that exercises PSA is app_km4 (the driver image) — see
# component/amebad/app_km4/{tfm_psa_selftest.c,CMakeLists.txt}.
