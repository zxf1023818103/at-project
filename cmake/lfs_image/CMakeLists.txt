# Host-toolchain sub-project that produces the `mklittlefs` LittleFS
# image builder. Invoked via ExternalProject_Add() from
# cmake/lfs_image.cmake so it stays in a separate cache from the outer
# cross-compile build (CMAKE_TOOLCHAIN_FILE for the outer is
# intentionally not propagated).
#
# Reuses our project-tree littlefs (component/littlefs) rather than
# initialising mklittlefs's own nested littlefs submodule — we already
# vendor upstream littlefs-project/littlefs at component/littlefs, and
# letting both copies coexist would just waste a git submodule + create
# a "which lfs.c am I building against?" ambiguity.
#
# Inputs (CMake args from the outer ExternalProject_Add):
#   MKLITTLEFS_SOURCE_DIR  — earlephilhower/mklittlefs root (main.cpp,
#                            tclap/, include/).
#   LITTLEFS_SOURCE_DIR    — upstream littlefs root (lfs.c, lfs_util.c,
#                            lfs.h).
#
# Output: mklittlefs (or mklittlefs.exe on Windows) installed into
# CMAKE_INSTALL_PREFIX.

cmake_minimum_required(VERSION 3.19)
project(mklittlefs C CXX)

if(NOT MKLITTLEFS_SOURCE_DIR)
    message(FATAL_ERROR "MKLITTLEFS_SOURCE_DIR must be set (the earlephilhower/mklittlefs source root)")
endif()
if(NOT LITTLEFS_SOURCE_DIR)
    message(FATAL_ERROR "LITTLEFS_SOURCE_DIR must be set (upstream littlefs root)")
endif()

add_executable(mklittlefs
    ${MKLITTLEFS_SOURCE_DIR}/main.cpp
    ${LITTLEFS_SOURCE_DIR}/lfs.c
    ${LITTLEFS_SOURCE_DIR}/lfs_util.c
)

# mklittlefs's main.cpp expects four -D macros that its upstream Makefile
# provides. Reproduce them here so the same source compiles cleanly under
# cmake. git describe of the two submodule trees gives nice provenance in
# `mklittlefs --version` output without depending on env state.
find_package(Git QUIET)
function(_git_describe out_var dir)
    if(GIT_FOUND AND EXISTS ${dir}/.git)
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
_git_describe(_littlefs_version   ${LITTLEFS_SOURCE_DIR})

target_compile_definitions(mklittlefs PRIVATE
    VERSION="${_mklittlefs_version}"
    LITTLEFS_VERSION="${_littlefs_version}"
    BUILD_CONFIG=""
    BUILD_CONFIG_NAME="-generic"
)

target_include_directories(mklittlefs PRIVATE
    ${MKLITTLEFS_SOURCE_DIR}
    ${MKLITTLEFS_SOURCE_DIR}/tclap
    ${MKLITTLEFS_SOURCE_DIR}/include
    # littlefs's own sources #include "lfs.h" unqualified — needs its
    # directory on the include path.
    ${LITTLEFS_SOURCE_DIR}
    # main.cpp does #include "littlefs/lfs.h" with the directory prefix.
    # Add the parent so that prefix resolves to component/littlefs/lfs.h.
    # ARMmbed/littlefs (mklittlefs's own nested submodule, which we don't
    # init) would have satisfied this from inside the source tree; reusing
    # our component/littlefs needs an extra -I.
    ${LITTLEFS_SOURCE_DIR}/..
)

# Upstream littlefs + mklittlefs's main.cpp trigger a handful of benign
# warnings under -Wall (printf format mismatches, signed/unsigned
# comparisons, unused result of read()). Silence them — we don't audit
# the upstream sources.
target_compile_options(mklittlefs PRIVATE -O2 -Wall -Wno-format -Wno-unused-result -Wno-sign-compare)

# mklittlefs's main.cpp uses C++11 features (std::string, range-for, ...).
set_target_properties(mklittlefs PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS mklittlefs RUNTIME DESTINATION bin)
