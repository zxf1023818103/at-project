# Host-toolchain sub-project that produces the `mklittlefs` LittleFS
# image builder. Invoked via ExternalProject_Add() from
# cmake/lfs_image.cmake so it stays in a separate cache from the outer
# cross-compile build (CMAKE_TOOLCHAIN_FILE for the outer is
# intentionally not propagated).
#
# Source tree (component/mklittlefs, the at-project branch of our fork):
#   main.cpp           — mklittlefs's command-line entry
#   tclap/             — inline header-only argument parser
#   include/           — mklittlefs's own headers
#   littlefs/          — RELATIVE SYMLINK -> ../littlefs (component/littlefs)
#
# The symlink is the whole point of the fork: it lets us reuse our
# project-tree upstream littlefs (component/littlefs, pinned at v2.8.2 —
# the same source the runtime SDK adapters use) instead of pulling the
# upstream ARMmbed/littlefs nested submodule. mklittlefs's own source
# layout (lfs.c at littlefs/lfs.c, #include "littlefs/lfs.h") therefore
# just works without any include-path gymnastics here.
#
# Input (CMake arg from the outer ExternalProject_Add):
#   MKLITTLEFS_SOURCE_DIR — earlephilhower/mklittlefs source root
#                          (our fork's at-project branch).
#
# Output: mklittlefs (or mklittlefs.exe on Windows) installed into
# CMAKE_INSTALL_PREFIX.

cmake_minimum_required(VERSION 3.19)
project(mklittlefs C CXX)

if(NOT MKLITTLEFS_SOURCE_DIR)
    message(FATAL_ERROR "MKLITTLEFS_SOURCE_DIR must be set (the earlephilhower/mklittlefs source root)")
endif()

set(_lfs_dir ${MKLITTLEFS_SOURCE_DIR}/littlefs)
if(NOT EXISTS ${_lfs_dir}/lfs.c)
    message(FATAL_ERROR
        "mklittlefs's littlefs symlink does not resolve to a real littlefs tree "
        "(missing ${_lfs_dir}/lfs.c). Make sure component/littlefs is checked out.")
endif()

add_executable(mklittlefs
    ${MKLITTLEFS_SOURCE_DIR}/main.cpp
    ${_lfs_dir}/lfs.c
    ${_lfs_dir}/lfs_util.c
)

target_include_directories(mklittlefs PRIVATE
    ${MKLITTLEFS_SOURCE_DIR}
    ${MKLITTLEFS_SOURCE_DIR}/tclap
    ${MKLITTLEFS_SOURCE_DIR}/include
    ${_lfs_dir}
)

# Upstream littlefs + mklittlefs's main.cpp trigger a handful of benign
# warnings under -Wall (printf format mismatches, signed/unsigned
# comparisons, unused result of read()). Silence them — we don't audit
# the upstream sources.
target_compile_options(mklittlefs PRIVATE -O2 -Wall -Wno-format -Wno-unused-result -Wno-sign-compare)

set_target_properties(mklittlefs PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

# mklittlefs's main.cpp expects four -D macros that its upstream Makefile
# provides. Reproduce them here so the same source compiles cleanly under
# cmake. git describe of each tree gives nice provenance in `mklittlefs
# --version` output.
find_package(Git QUIET)
function(_git_describe out_var dir)
    set(_out "")
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C ${dir} describe --tags --always --dirty
            OUTPUT_VARIABLE _out
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
    if(NOT _out)
        set(_out "unknown")
    endif()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

_git_describe(_mklittlefs_version ${MKLITTLEFS_SOURCE_DIR})
# The littlefs symlink target — git describe follows symlinks, so this
# reads the version of component/littlefs.
_git_describe(_littlefs_version ${_lfs_dir})

target_compile_definitions(mklittlefs PRIVATE
    VERSION="${_mklittlefs_version}"
    LITTLEFS_VERSION="${_littlefs_version}"
    BUILD_CONFIG=""
    BUILD_CONFIG_NAME="-generic"
)

install(TARGETS mklittlefs RUNTIME DESTINATION bin)
