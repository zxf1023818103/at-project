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

include(proj.conf)

# Pull the at-project root in as a single subdir — root CMakeLists.txt
# is the canonical entry, it add_subdirectory's component/ + src/ + any
# future top-level dirs. Add new at-project dirs there, not here.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../..
                 ${CMAKE_BINARY_DIR}/at_project)

bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(at PRIVATE bouffalo_sdk)
