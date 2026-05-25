include_guard()

# Host-side LittleFS image builder.
#
# Provides:
#   lfs_image_add(<output_path>
#                 SOURCE_DIR <dir>
#                 [BLOCK_SIZE <n>] [READ_SIZE <n>] [PROG_SIZE <n>]
#                 [IMAGE_SIZE <n>] [TARGET <cmake-target-name>])
#
# Builds mklfs (the Lua-RTOS-ESP32 upstream image tool that littlefs's
# own README recommends — vendored at component/mklfs/) together with
# upstream littlefs's lfs.c / lfs_util.c from component/littlefs/, as a
# host-toolchain CMake sub-project via ExternalProject_Add, then runs
# the resulting native binary to pack <SOURCE_DIR> into a LittleFS image
# at <output_path>.
#
# Why a sub-project: an outer cross-compile build cannot also produce a
# native host binary in the same CMake project — CMAKE_C_COMPILER is a
# single cache value, fixed at the first enable_language(C). Using
# ExternalProject_Add gives the host build its own cache and toolchain
# (no CMAKE_TOOLCHAIN_FILE inherited), so cross + host coexist cleanly.
#
# Compiling mklfs from source rather than running the prebuilt
# platform/bouffalo_sdk/tools/genlfs/mklfs-{ubuntu,macos}|mklfs.exe makes
# the build host-platform-independent.
#
# Defaults match doc/lfs.md's cross-platform LFS params, used by the
# ameba / amebad / amebadplus / amebagreen2 / bl602 / bl616 / bl616cl
# runtime adapters:
#   BLOCK_SIZE = 4096
#   READ_SIZE  = 256
#   PROG_SIZE  = 256
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
        READ_SIZE
        PROG_SIZE
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
    if(NOT LFS_READ_SIZE)
        set(LFS_READ_SIZE 256)
    endif()
    if(NOT LFS_PROG_SIZE)
        set(LFS_PROG_SIZE 256)
    endif()
    if(NOT LFS_IMAGE_SIZE)
        set(LFS_IMAGE_SIZE 1024000)
    endif()
    if(NOT LFS_TARGET)
        set(LFS_TARGET lfs_image)
    endif()

    set(_mklfs_install "${CMAKE_BINARY_DIR}/_mklfs_install")
    set(_mklfs_bin "${_mklfs_install}/bin/mklfs${CMAKE_HOST_EXECUTABLE_SUFFIX}")

    # Host sub-project. CMAKE_TOOLCHAIN_FILE intentionally not propagated
    # (cmake -P-style ExternalProject_Add does not inherit it by default,
    # but we override CMAKE_SYSTEM_NAME explicitly to the host name so
    # any toolchain leftovers in the parent environment don't sneak in).
    # BUILD_BYPRODUCTS declares the install path so Ninja knows which
    # file the sub-build produces.
    if(NOT TARGET mklfs_host)
        ExternalProject_Add(mklfs_host
            SOURCE_DIR ${AT_PROJECT_ROOT}/cmake/lfs_image
            BINARY_DIR ${CMAKE_BINARY_DIR}/_mklfs_build
            INSTALL_DIR ${_mklfs_install}
            CMAKE_GENERATOR ${CMAKE_GENERATOR}
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_SYSTEM_NAME=${CMAKE_HOST_SYSTEM_NAME}
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DMKLFS_SOURCE_DIR=${AT_PROJECT_ROOT}/component/mklfs
                -DLITTLEFS_SOURCE_DIR=${AT_PROJECT_ROOT}/component/littlefs
            BUILD_BYPRODUCTS ${_mklfs_bin}
        )
    endif()

    # Re-glob resource files at every cmake configure run; CONFIGURE_DEPENDS
    # tells cmake to re-run if files are added/removed under SOURCE_DIR.
    file(GLOB_RECURSE _resource_files CONFIGURE_DEPENDS
        "${LFS_SOURCE_DIR}/*")

    # mklfs treats `-c <dir>` literally and uses strchr(src,'/') to split
    # the host path into "host prefix" + "lfs in-image path". Passing an
    # absolute -c argument therefore puts the entire host prefix inside
    # the image (lfs_file_open fails NOENT on the bogus parent dirs).
    # Run mklfs from the source's parent dir and pass its basename so the
    # host prefix has no '/' and only the per-file suffix becomes the lfs
    # path. -i still takes an absolute output so the bin lands where the
    # caller asked regardless of where mklfs is run from.
    get_filename_component(_src_parent "${LFS_SOURCE_DIR}" DIRECTORY)
    get_filename_component(_src_name   "${LFS_SOURCE_DIR}" NAME)

    add_custom_command(
        OUTPUT "${target_path}"
        COMMAND "${_mklfs_bin}"
            -c "${_src_name}"
            -b ${LFS_BLOCK_SIZE}
            -r ${LFS_READ_SIZE}
            -p ${LFS_PROG_SIZE}
            -s ${LFS_IMAGE_SIZE}
            -i "${target_path}"
        WORKING_DIRECTORY "${_src_parent}"
        DEPENDS mklfs_host ${_resource_files}
        COMMENT "[lfs_image] mklfs -> ${target_path} (${LFS_IMAGE_SIZE} bytes, block=${LFS_BLOCK_SIZE})"
        VERBATIM
    )

    add_custom_target(${LFS_TARGET} ALL DEPENDS "${target_path}")
endfunction()
