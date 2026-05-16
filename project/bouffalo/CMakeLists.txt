cmake_minimum_required(VERSION 3.15)
include(proj.conf)
find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})
sdk_add_include_directories(./include)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../src)

file(GLOB private_sources
    CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.c"
    LIST_DIRECTORIES false)

project(at)
target_sources(${proj_name}.elf PRIVATE ${private_sources})
