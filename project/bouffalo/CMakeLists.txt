cmake_minimum_required(VERSION 3.20)
project(at C CXX)

# CMP0118 NEW (CMake 3.20+) makes the GENERATED source-file property
# visible across directory boundaries. Required because src/CMakeLists.txt
# (pulled in via add_subdirectory below) registers a custom command for
# the ragel-generated AtCommandParser.rl.cpp; without this policy,
# bflb_add_executable(at ${AT_USER_SOURCES}) in this directory wouldn't
# know that the .cpp is a build product and would error
# "Cannot find source file".
cmake_policy(SET CMP0118 NEW)

set(AT_PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND CMAKE_MODULE_PATH ${AT_PROJECT_ROOT}/cmake)

include(proj.conf)

add_subdirectory(${AT_PROJECT_ROOT}/component
                 ${CMAKE_BINARY_DIR}/component)

add_subdirectory(${AT_PROJECT_ROOT}/src ${CMAKE_BINARY_DIR}/src)

bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(at PRIVATE bouffalo_sdk)
