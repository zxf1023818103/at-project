include_guard()

# Pulls in component/lfs_easyflash (EasyFlash KV API on top of upstream
# littlefs). Wired by component/CMakeLists.txt's auto-enumeration when
# the dir exists; also invokable standalone by legacy project scripts.
#
# Hard prereqs that must already be registered as targets when this is
# included:
#   littlefs            from cmake/littlefs.cmake
#   bflb_littlefs_port  from component/bouffalo_sdk
#   freertos_kernel + bflb_log
#
# lfs_easyflash itself only declares those as target_link_libraries —
# the wiring up the chain (cmake/littlefs.cmake, bouffalo_sdk umbrella)
# is done elsewhere.

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/lfs_easyflash.cmake")
endif()

if(NOT TARGET littlefs)
    message(FATAL_ERROR
        "cmake/lfs_easyflash.cmake: littlefs target missing. "
        "Include cmake/littlefs.cmake before lfs_easyflash.cmake.")
endif()

add_subdirectory(${AT_PROJECT_ROOT}/component/lfs_easyflash
                 ${CMAKE_BINARY_DIR}/at_lfs_easyflash_build
                 EXCLUDE_FROM_ALL)
