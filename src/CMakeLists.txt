cmake_minimum_required(VERSION 3.16)
project(at_user C CXX ASM)

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

add_library(at_user STATIC ${AT_USER_SOURCES})
target_include_directories(at_user PUBLIC ${AT_USER_SRC_DIR})
