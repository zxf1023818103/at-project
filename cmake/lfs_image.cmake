include_guard()

# Host-side LittleFS image builder.
#
# Provides:
#   lfs_image_add(<output_path>
#                 SOURCE_DIR <dir>
#                 [BLOCK_SIZE <n>] [PAGE_SIZE <n>] [IMAGE_SIZE <n>]
#                 [TARGET <cmake-target-name>])
#
# Builds earlephilhower/mklittlefs from source as a host-toolchain
# CMake sub-project via ExternalProject_Add, then runs the resulting
# native binary to pack <SOURCE_DIR> into a LittleFS image at
# <output_path>.
#
# mklittlefs sources live at component/mklittlefs/ (vendored as a git
# submodule). Its nested ./littlefs submodule is intentionally NOT
# initialised — the host build reuses our project-tree littlefs at
# component/littlefs/ instead, so we don't carry two slightly-different
# copies of upstream littlefs.
#
# Why a sub-project: an outer cross-compile build cannot also produce a
# native host binary in the same CMake project — CMAKE_C_COMPILER and
# CMAKE_CXX_COMPILER are single cache values fixed at the first
# enable_language. ExternalProject_Add gives the host build its own
# cache and toolchain (no CMAKE_TOOLCHAIN_FILE inherited), so cross +
# host coexist cleanly.
#
# Defaults match doc/lfs.md's cross-platform LFS params:
#   BLOCK_SIZE = 4096   (must match the runtime adapter's lfs_config)
#   PAGE_SIZE  = 256    (mklittlefs accepts it but hardcodes its own
#                        internal lfs_config to 64 — passed through for
#                        documentation only; disk format depends only
#                        on block_size, so runtime mount works
#                        regardless)
#   IMAGE_SIZE = 1024000
#   TARGET     = lfs_image

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/lfs_image.cmake")
endif()

include(ExternalProject)

function(lfs_image_add target_path)
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

    # Host sub-project. Override CMAKE_SYSTEM_NAME so any toolchain
    # leftovers in the parent environment don't sneak in.
    # BUILD_BYPRODUCTS declares the install path so Ninja knows which
    # file the sub-build produces (transparent dependency edge from the
    # custom_command below).
    if(NOT TARGET mklittlefs_host)
        ExternalProject_Add(mklittlefs_host
            SOURCE_DIR ${AT_PROJECT_ROOT}/cmake/lfs_image
            BINARY_DIR ${CMAKE_BINARY_DIR}/_mklittlefs_build
            INSTALL_DIR ${_install_dir}
            CMAKE_GENERATOR ${CMAKE_GENERATOR}
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DMKLITTLEFS_SOURCE_DIR=${AT_PROJECT_ROOT}/component/mklittlefs
                -DLITTLEFS_SOURCE_DIR=${AT_PROJECT_ROOT}/component/littlefs
            BUILD_BYPRODUCTS ${_mklittlefs_bin}
        )
    endif()

    # Re-glob resource files at every cmake configure run; CONFIGURE_DEPENDS
    # tells cmake to re-run if files are added/removed under SOURCE_DIR.
    file(GLOB_RECURSE _resource_files CONFIGURE_DEPENDS
        "${LFS_SOURCE_DIR}/*")

    # mklittlefs takes the pack-dir via -c and the output image as a
    # POSITIONAL argument (unlike Bouffalo's old mklfs which used -i).
    # Run with WORKING_DIRECTORY = SOURCE_DIR's parent + -c <basename>
    # so the in-image paths look like /<basename>/file rather than
    # /full/host/abs/path/file.
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
