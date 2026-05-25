# bflb_elf_to_bin.cmake
#
# Provides:
#   bflb_add_executable(<target> [BOARD <board>] <sources...>)
#
# Builds an ELF for the chip selected by the active Bouffalo toolchain
# (bl602/bl616/bl616cl), then runs tool/bflb_elf_to_bin.py against it to
# produce the flashable artefacts under <bindir>/build_out/:
#
#   <target>_<chip>.elf
#   <target>_<chip>.bin
#   <target>_<chip>.bin.hash
#   <target>_<chip>.xz
#   <target>_<chip>.xz.hash
#   <target>_<chip>.xz.ota
#   <target>_<chip>_whole_flash_data.bin
#
# BOARD defaults to ${BOUFFALO_BOARD}, or to ${BOUFFALO_CHIP}dk if neither
# is set (mirrors the convention used by project/bouffalo/Makefile).

if(NOT DEFINED BFLB_ELF_TO_BIN_SCRIPT)
    # REALPATH so a symlinked cmake/ resolves to the real repo cmake/ before
    # we walk up to ../tool/.
    get_filename_component(_bflb_self "${CMAKE_CURRENT_LIST_FILE}" REALPATH)
    get_filename_component(_bflb_self_dir "${_bflb_self}" DIRECTORY)
    set(BFLB_ELF_TO_BIN_SCRIPT
        "${_bflb_self_dir}/../tool/bflb_elf_to_bin.py"
        CACHE FILEPATH "Path to bflb_elf_to_bin.py"
    )
    unset(_bflb_self)
    unset(_bflb_self_dir)
endif()

if(NOT DEFINED BFLB_PYTHON)
    find_program(BFLB_PYTHON NAMES python3 python REQUIRED)
endif()

function(bflb_add_executable TARGET)
    if(NOT BOUFFALO_CHIP)
        message(FATAL_ERROR
            "bflb_add_executable(${TARGET}): BOUFFALO_CHIP is not set — "
            "include a chip toolchain (cmake/bl602-toolchain.cmake etc.)"
        )
    endif()

    cmake_parse_arguments(_BFLB "" "BOARD" "" ${ARGN})
    if(_BFLB_BOARD)
        set(_board "${_BFLB_BOARD}")
    elseif(BOUFFALO_BOARD)
        set(_board "${BOUFFALO_BOARD}")
    else()
        set(_board "${BOUFFALO_CHIP}dk")
    endif()

    set(_base "${TARGET}_${BOUFFALO_CHIP}")

    add_executable(${TARGET} ${_BFLB_UNPARSED_ARGUMENTS})
    set_target_properties(${TARGET} PROPERTIES
        OUTPUT_NAME "${_base}"
        SUFFIX ".elf"
    )

    # Always emit a linker .map next to the ELF — debugging/sizing without
    # one is painful and it costs nothing. Lives here (not in the SDK
    # umbrella's INTERFACE link options) because $<TARGET_PROPERTY:OUTPUT_NAME>
    # would resolve at the wrong time for a transitive INTERFACE setting on
    # consumers that haven't run set_target_properties yet.
    target_link_options(${TARGET} PRIVATE
        "-Wl,-Map=${_base}.map"
    )

    # No --whole-archive wrapping today. The legacy SDK uses
    # -Wl,--whole-archive around its SDK libs to keep `.itcm_region`
    # at ~4 KB (the LDS wildcards otherwise snowball to ~24 KB via
    # bl602_*.o(.rodata) function-pointer tables that keep their .text
    # alive through gc-sections). Reproducing it under CMake 3.28 needs
    # link-line de-dup (CMP0156 is 3.29+), reconciling
    # bflb_mbedtls_port/bignum.c vs upstream library/bignum.c (full
    # mbedtls_mpi_* overlap), and feature-unification across cascading
    # INTERFACE umbrellas. We hit each of those in turn; the project's
    # bl<chip>_flash.ld bumps ITCM 16K → 24K as the simpler workaround
    # for now. See cmake/at_dedup_link_libs.cmake (which stays in-tree
    # as a starting point) for the dedup helper.

    # ${_base}.elf is already produced by add_executable() above — leaving it
    # out of OUTPUT avoids a "multiple rules produce …" error from ninja.
    set(_outputs
        "${CMAKE_CURRENT_BINARY_DIR}/${_base}_whole_flash_data.bin"
        "${CMAKE_CURRENT_BINARY_DIR}/${_base}.bin"
        "${CMAKE_CURRENT_BINARY_DIR}/${_base}.bin.hash"
        "${CMAKE_CURRENT_BINARY_DIR}/${_base}.xz"
        "${CMAKE_CURRENT_BINARY_DIR}/${_base}.xz.hash"
        "${CMAKE_CURRENT_BINARY_DIR}/${_base}.xz.ota"
    )

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND "${BFLB_PYTHON}" "${BFLB_ELF_TO_BIN_SCRIPT}"
                --chip      ${BOUFFALO_CHIP}
                --board     ${_board}
                --name      ${TARGET}
                --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
                --elf       "$<TARGET_FILE:${TARGET}>"
                --objcopy   "${CMAKE_OBJCOPY}"
        DEPENDS ${TARGET} "${BFLB_ELF_TO_BIN_SCRIPT}"
        COMMENT "[bflb] ${TARGET}: ELF -> ${_base}_whole_flash_data.bin (chip=${BOUFFALO_CHIP}, board=${_board})"
        VERBATIM
    )

    add_custom_target(${TARGET}_bin ALL DEPENDS ${_outputs})
endfunction()
