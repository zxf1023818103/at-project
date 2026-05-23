cmake_minimum_required(VERSION 3.16)
project(at_project C CXX ASM)

# Root entry for the at_project unified build (currently used by the
# AmebaD SDK's library/Makefile cmake invocation). Bouffalo and the new
# ameba-rtos SDK don't drive a unified CMake build of this tree; they
# include cmake/zlib.cmake etc. as standalone fragments from inside
# their own project setups.

set(AT_PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR})
list(APPEND CMAKE_MODULE_PATH ${AT_PROJECT_ROOT}/cmake)

add_subdirectory(component)
add_subdirectory(src)
