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


set(AT_USER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# (The ragel-generated AT-command parser + the whole C++ task/UART/BT framework were deleted --
# dead since the console moved to FreeRTOS-Plus-CLI and BLE to NimBLE. ragel is no longer a
# build prerequisite.)
file(GLOB AT_USER_SOURCES CONFIGURE_DEPENDS
    "${AT_USER_SRC_DIR}/*.c"
    "${AT_USER_SRC_DIR}/*.cpp"
    LIST_DIRECTORIES false)

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
