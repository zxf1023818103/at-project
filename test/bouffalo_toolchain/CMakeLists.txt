cmake_minimum_required(VERSION 3.16)
project(bouffalo_toolchain_test C ASM)

if(NOT DEFINED BOUFFALO_CHIP)
    message(FATAL_ERROR
        "BOUFFALO_CHIP is not set. Configure with one of the chip toolchains, e.g.\n"
        "  cmake -S . -B build/bl616 -DCMAKE_TOOLCHAIN_FILE=cmake/bl616-toolchain.cmake")
endif()

# `cmake` and `component` are symlinks into the repo root (../../{cmake,component}),
# which lets us use the regular relative-path forms of include() and
# add_subdirectory() without explicit binary-dir or absolute-path workarounds.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(bflb_elf_to_bin)

message(STATUS "Bouffalo toolchain smoke test")
message(STATUS "  CHIP  = ${BOUFFALO_CHIP}")
message(STATUS "  MARCH = ${BOUFFALO_MARCH}")
message(STATUS "  MABI  = ${BOUFFALO_MABI}")
message(STATUS "  MTUNE = ${BOUFFALO_MTUNE}")

add_subdirectory(component/bouffalo_sdk)

bflb_add_executable(smoke main.c)
target_link_libraries(smoke PRIVATE bouffalo_sdk)

# Convenience target: dump ELF attributes off the linked executable to confirm
# the chip's -march/-mabi reached every TU.
get_filename_component(_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(BOUFFALO_READELF
    NAMES riscv64-unknown-elf-readelf
    HINTS "${_compiler_bin}"
    NO_DEFAULT_PATH
)
if(BOUFFALO_READELF)
    add_custom_target(show-attrs
        COMMAND ${BOUFFALO_READELF} -A $<TARGET_FILE:smoke>
        DEPENDS smoke
        COMMENT "ELF attributes for ${BOUFFALO_CHIP} build (expect Arch=${BOUFFALO_MARCH})"
        VERBATIM
    )
endif()
