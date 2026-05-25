# Shared user sources for all platform projects.
#
# Sourced two ways:
#   - via add_subdirectory(<at-project>/src ...) from a chip project.
#     AT_USER_SOURCES and AT_USER_SRC_DIR get propagated to the parent
#     scope so the project can feed them into the platform's own
#     add_executable / ameba_add_internal_library / etc.
#   - via add_subdirectory(src) from the at-project root, when the AmebaD
#     SDK invokes `cmake -S <at-project-root>` to produce user_link.list.
#     In that flow we emit `at_user` STATIC + the user_link.list file
#     the SDK Make build then reads at link time.
#
# The ragel-generated parser lives in <bindir>/AtCommandParser.rl.cpp
# (regenerated per build dir; src/.gitignore drops stale local copies).

set(AT_USER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

find_program(RAGEL_EXECUTABLE ragel REQUIRED)
set(_at_ragel_out ${CMAKE_CURRENT_BINARY_DIR}/AtCommandParser.rl.cpp)
add_custom_command(
    OUTPUT  ${_at_ragel_out}
    COMMAND ${RAGEL_EXECUTABLE} -G2 -o ${_at_ragel_out} ${AT_USER_SRC_DIR}/AtCommandParser.rl
    DEPENDS ${AT_USER_SRC_DIR}/AtCommandParser.rl
    COMMENT "ragel -G2 AtCommandParser.rl -> ${_at_ragel_out}"
    VERBATIM
)

# add_custom_command's rule is only emitted by CMake when a target in
# the SAME directory references its OUTPUT — even with CMP0118 NEW (which
# only fixes the GENERATED property scope, not the rule-emission scope).
# In the add_subdirectory(src) path the consuming target lives in the
# parent project, so without this same-dir handle ninja never learns the
# ragel command. The target itself is just a marker; nobody depends on it.
add_custom_target(at_user_ragel DEPENDS ${_at_ragel_out})

file(GLOB AT_USER_SOURCES CONFIGURE_DEPENDS
    "${AT_USER_SRC_DIR}/*.c"
    "${AT_USER_SRC_DIR}/*.cpp"
    LIST_DIRECTORIES false)
# Drop any stale src/AtCommandParser.rl.cpp picked up by the glob — we want the
# build-dir copy to be the only one.
list(FILTER AT_USER_SOURCES EXCLUDE REGEX "/AtCommandParser\\.rl\\.cpp$")
list(APPEND AT_USER_SOURCES ${_at_ragel_out})

# Always propagate to whatever scope called us.
set(AT_USER_SOURCES ${AT_USER_SOURCES} PARENT_SCOPE)
set(AT_USER_SRC_DIR ${AT_USER_SRC_DIR} PARENT_SCOPE)

# ---------------------------------------------------------------------------
# AmebaD legacy flow — emit at_user STATIC + user_link.list when the
# at-project root is the source root (AmebaD SDK's library/Makefile invokes
# `cmake -S <at-project-root>`). Bouffalo wraps the root via add_subdirectory
# from project/bouffalo, so CMAKE_SOURCE_DIR there is project/bouffalo and
# this branch stays inactive.
# ---------------------------------------------------------------------------
get_filename_component(_at_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(CMAKE_SOURCE_DIR STREQUAL _at_root)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/at_collect_static_libs.cmake)

    add_library(at_user STATIC ${AT_USER_SOURCES})
    target_include_directories(at_user PUBLIC ${AT_USER_SRC_DIR})
    target_link_libraries(at_user PRIVATE zlibstatic)

    # Walk at_user's transitive deps and write the resolved STATIC_LIBRARY
    # .a paths to user_link.list.
    at_collect_static_libs(at_user AT_USER_STATIC_LIBS)
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/user_link.list"
        CONTENT "$<JOIN:${AT_USER_STATIC_LIBS},\n>\n")
endif()
