cmake_minimum_required(VERSION 3.15)
find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})
sdk_add_include_directories(.)
sdk_set_main_file(main.c)
project(pan)
# SDK assumes BUILD_DIR is relative when setting -T; use the actual binary dir.
set_target_properties(${proj_name}.elf PROPERTIES LINK_FLAGS "-T${CMAKE_CURRENT_BINARY_DIR}/generated/linker.ld -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/build_out/${proj_name}.map")
