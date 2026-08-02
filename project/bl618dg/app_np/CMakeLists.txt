# ---- standalone CMake project (libs in component/, this image here) ----
# Built via:
#   cmake -S project/bl618dg/app_np -B build/bl618dg_np \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/bl618dg-np-toolchain.cmake
# (the toolchain FORCE-sets BOUFFALO_CHIP=bl618dg + BOUFFALO_CPU_ID=np).
#
# Mirrors project/amebad/app_km0: the NP core is BL618DG's minimal core image.
# Its .bin is appended into the AP image by project/bl618dg/Makefile, which is
# how the SDK ships a dual-core BL618DG firmware (CONFIG_DUALCORE_NP_IMAGE).
cmake_minimum_required(VERSION 3.20)
project(app_np C CXX ASM)

get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(AT_ROOT "${AT_ROOT}" CACHE INTERNAL "at-project repo root")

if(NOT "${BOUFFALO_CPU_ID}" STREQUAL "np")
    message(FATAL_ERROR
        "app_np: configure with cmake/bl618dg-np-toolchain.cmake "
        "(BOUFFALO_CPU_ID='${BOUFFALO_CPU_ID}')")
endif()

add_subdirectory(${AT_ROOT}/component ${CMAKE_BINARY_DIR}/component)

bflb_add_executable(app_np BOARD bl618dgdk main.c)
target_link_libraries(app_np PRIVATE bouffalo_sdk)
