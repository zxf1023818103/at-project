# Host-side LittleFS image builder.
#
# Dual-mode file:
#
#   1. add_subdirectory(component/lfs_image) from the outer (typically
#      cross-compile) build — registers the helper function
#
#        lfs_image_add(<output_path>
#                      SOURCE_DIR <dir>
#                      [BLOCK_SIZE <n>] [PAGE_SIZE <n>] [IMAGE_SIZE <n>]
#                      [TARGET <cmake-target-name>])
#
#      which spawns the standalone host sub-build below via
#      ExternalProject_Add (so target + host compilers stay in separate
#      CMake caches — a single project can only have one CMAKE_C_COMPILER).
#
#   2. cmake -S component/lfs_image (the ExternalProject_Add path above) —
#      builds earlephilhower/mklittlefs against upstream littlefs, both
#      vendored next to this file:
#        component/lfs_image/mklittlefs/    fork at zxf1023818103/mklittlefs
#                                            (at-project branch — only diff
#                                             from upstream is the symlink
#                                             ./littlefs -> ../../littlefs)
#        component/littlefs/                upstream littlefs (v2.8.2),
#                                            reached through the symlink
#                                            so the runtime SDK adapters
#                                            and the host image builder
#                                            share the exact same sources.
#
# Defaults match doc/lfs.md's cross-platform LFS params:
#   BLOCK_SIZE = 4096   (must match the runtime adapter's lfs_config)
#   PAGE_SIZE  = 256    (mklittlefs hardcodes its own cache_size etc. to
#                        64 internally; disk format depends only on
#                        block_size, so runtime mount works regardless)
#   IMAGE_SIZE = 1024000
#   TARGET     = lfs_image

# ---------------------------------------------------------------------
# Standalone mode: this directory is the top of a CMake project. Build
# the mklittlefs binary and exit.
# ---------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    cmake_minimum_required(VERSION 3.19)
    project(mklittlefs C CXX)

    set(_mklittlefs_dir ${CMAKE_CURRENT_LIST_DIR}/mklittlefs)
    set(_littlefs_dir   ${_mklittlefs_dir}/littlefs)  # symlink to ../../littlefs

    if(NOT EXISTS ${_mklittlefs_dir}/main.cpp)
        message(FATAL_ERROR
            "${_mklittlefs_dir} is empty — run `git submodule update --init` "
            "to fetch component/lfs_image/mklittlefs.")
    endif()
    if(NOT EXISTS ${_littlefs_dir}/lfs.c)
        message(FATAL_ERROR
            "mklittlefs's ./littlefs symlink does not resolve to a real "
            "littlefs tree (missing ${_littlefs_dir}/lfs.c). On Windows make "
            "sure git symlinks are enabled (see doc/lfs.md).")
    endif()

    add_executable(mklittlefs
        ${_mklittlefs_dir}/main.cpp
        ${_littlefs_dir}/lfs.c
        ${_littlefs_dir}/lfs_util.c
    )
    target_include_directories(mklittlefs PRIVATE
        ${_mklittlefs_dir}
        ${_mklittlefs_dir}/tclap
        ${_mklittlefs_dir}/include
        ${_littlefs_dir}
    )
    target_compile_options(mklittlefs PRIVATE
        -O2 -Wall -Wno-format -Wno-unused-result -Wno-sign-compare
    )
    set_target_properties(mklittlefs PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
    )

    # Reproduce the four -D macros mklittlefs's upstream Makefile feeds
    # in (used by its --version output). git describe of each submodule
    # gives nice provenance without any env-state dependency.
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
    _git_describe(_mklittlefs_version ${_mklittlefs_dir})
    _git_describe(_littlefs_version   ${_littlefs_dir})
    target_compile_definitions(mklittlefs PRIVATE
        VERSION="${_mklittlefs_version}"
        LITTLEFS_VERSION="${_littlefs_version}"
        BUILD_CONFIG=""
        BUILD_CONFIG_NAME="-generic"
    )

    install(TARGETS mklittlefs RUNTIME DESTINATION bin)
    return()
endif()

# ---------------------------------------------------------------------
# Outer-build mode: pulled in by add_subdirectory() from
# component/CMakeLists.txt's enumeration. Just define the helper; do
# nothing heavy until a consumer actually calls lfs_image_add().
# ---------------------------------------------------------------------
include_guard()
include(ExternalProject)

function(lfs_image_add target_path)
    # CMAKE_CURRENT_FUNCTION_LIST_DIR (CMake 3.17+) resolves to the dir
    # of the file that DEFINED this function — i.e. this very directory
    # (component/lfs_image), regardless of where the caller lives.
    # Plain CMAKE_CURRENT_LIST_DIR would resolve to the caller, and a
    # set() at file-load scope wouldn't survive add_subdirectory's scope
    # exit.
    set(_self_dir ${CMAKE_CURRENT_FUNCTION_LIST_DIR})
    set(_one_value_args
        SOURCE_DIR
        BLOCK_SIZE
        PAGE_SIZE
        IMAGE_SIZE
        TARGET
    )
    cmake_parse_arguments(LFS "" "${_one_value_args}" "" ${ARGN})

    if(NOT LFS_SOURCE_DIR)
        message(FATAL_ERROR "lfs_image_add: SOURCE_DIR is required")
    endif()
    if(NOT EXISTS "${LFS_SOURCE_DIR}")
        message(FATAL_ERROR "lfs_image_add: SOURCE_DIR ${LFS_SOURCE_DIR} does not exist")
    endif()

    if(NOT LFS_BLOCK_SIZE)
        set(LFS_BLOCK_SIZE 4096)
    endif()
    if(NOT LFS_PAGE_SIZE)
        set(LFS_PAGE_SIZE 256)
    endif()
    if(NOT LFS_IMAGE_SIZE)
        set(LFS_IMAGE_SIZE 1024000)
    endif()
    if(NOT LFS_TARGET)
        set(LFS_TARGET lfs_image)
    endif()

    set(_install_dir "${CMAKE_BINARY_DIR}/_mklittlefs_install")
    set(_mklittlefs_bin "${_install_dir}/bin/mklittlefs${CMAKE_HOST_EXECUTABLE_SUFFIX}")

    # Host sub-project. Override CMAKE_SYSTEM_NAME so toolchain leftovers
    # in the parent environment don't sneak in. BUILD_BYPRODUCTS declares
    # the install path so Ninja knows which file the sub-build produces
    # (transparent dependency edge from the custom_command below).
    if(NOT TARGET mklittlefs_host)
        ExternalProject_Add(mklittlefs_host
            SOURCE_DIR ${_self_dir}
            BINARY_DIR ${CMAKE_BINARY_DIR}/_mklittlefs_build
            INSTALL_DIR ${_install_dir}
            CMAKE_GENERATOR ${CMAKE_GENERATOR}
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            BUILD_BYPRODUCTS ${_mklittlefs_bin}
        )
    endif()

    # Re-glob resource files at every cmake configure run; CONFIGURE_DEPENDS
    # tells cmake to re-run if files are added/removed under SOURCE_DIR.
    file(GLOB_RECURSE _resource_files CONFIGURE_DEPENDS
        "${LFS_SOURCE_DIR}/*")

    # mklittlefs takes the pack-dir via -c and the output image as a
    # POSITIONAL argument. Run from SOURCE_DIR's parent + -c <basename>
    # so in-image paths look like /<basename>/file rather than
    # /full/host/abs/path/file (mklittlefs' strchr(src,'/') path split).
    get_filename_component(_src_parent "${LFS_SOURCE_DIR}" DIRECTORY)
    get_filename_component(_src_name   "${LFS_SOURCE_DIR}" NAME)

    add_custom_command(
        OUTPUT "${target_path}"
        COMMAND "${_mklittlefs_bin}"
            -c "${_src_name}"
            -b ${LFS_BLOCK_SIZE}
            -p ${LFS_PAGE_SIZE}
            -s ${LFS_IMAGE_SIZE}
            "${target_path}"
        WORKING_DIRECTORY "${_src_parent}"
        DEPENDS mklittlefs_host ${_resource_files}
        COMMENT "[lfs_image] mklittlefs -> ${target_path} (${LFS_IMAGE_SIZE} bytes, block=${LFS_BLOCK_SIZE})"
        VERBATIM
    )

    add_custom_target(${LFS_TARGET} ALL DEPENDS "${target_path}")
endfunction()
