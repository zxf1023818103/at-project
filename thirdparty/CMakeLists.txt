# Enumerates immediate subdirs of thirdparty/ and pulls each in.
#
# Each library is wired up by a matching cmake/<libname>.cmake — that
# script is where the option knobs (BUILD_SHARED_LIBS, -Wno-..., etc.)
# and the actual add_subdirectory() call live, because every library
# needs its own per-target post-processing (e.g. zlibstatic gets -w to
# silence the upstream's loose warnings).
#
# To add a new third-party submodule:
#   1. git submodule add <url> thirdparty/<name>
#   2. drop a cmake/<name>.cmake that sets options + add_subdirectory +
#      any post-target_compile_options. Use include_guard() at the top.
#   3. wire the target into src/CMakeLists.txt via target_link_libraries.
# No edits to this file required.

file(GLOB _at_thirdparty_subdirs
    LIST_DIRECTORIES true
    RELATIVE ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/*)

foreach(_at_subdir IN LISTS _at_thirdparty_subdirs)
    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/${_at_subdir})
        continue()
    endif()
    set(_at_glue ${AT_PROJECT_ROOT}/cmake/${_at_subdir}.cmake)
    if(EXISTS ${_at_glue})
        include(${_at_glue})
    else()
        message(WARNING "thirdparty/${_at_subdir} has no cmake/${_at_subdir}.cmake glue, skipping")
    endif()
endforeach()
