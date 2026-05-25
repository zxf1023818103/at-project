# Enumerates immediate subdirs of component/ and pulls each in.
#
# Each library is wired up either:
#   - via a matching cmake/<libname>.cmake — a glue script that sets
#     option knobs (BUILD_SHARED_LIBS, -Wno-..., etc.) and the actual
#     add_subdirectory() call. This is the right pattern for third-party
#     submodules whose upstream CMakeLists.txt we can't modify but need
#     to wrap with project-specific post-processing.
#   - via component/<libname>/CMakeLists.txt living next to the source,
#     used when the wiring is project-owned (component/lfs_image is the
#     current example: a dual-mode CMakeLists that defines a helper
#     function on add_subdirectory and acts as a standalone host
#     sub-project under ExternalProject_Add).
#
# Glue script takes precedence when both exist.
#
# To add a new third-party submodule:
#   1. git submodule add <url> component/<name>
#   2. drop a cmake/<name>.cmake that sets options + add_subdirectory +
#      any post-target_compile_options. Use include_guard() at the top.
#   3. wire the target into src/CMakeLists.txt via target_link_libraries.
# No edits to this file required.

file(GLOB _at_component_subdirs
    LIST_DIRECTORIES true
    RELATIVE ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/*)

foreach(_at_subdir IN LISTS _at_component_subdirs)
    if(NOT IS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/${_at_subdir})
        continue()
    endif()
    set(_at_glue   ${AT_PROJECT_ROOT}/cmake/${_at_subdir}.cmake)
    set(_at_inline ${CMAKE_CURRENT_LIST_DIR}/${_at_subdir}/CMakeLists.txt)
    if(EXISTS ${_at_glue})
        include(${_at_glue})
    elseif(EXISTS ${_at_inline})
        add_subdirectory(${_at_subdir})
    else()
        message(WARNING "component/${_at_subdir} has neither cmake/${_at_subdir}.cmake nor an inline CMakeLists.txt, skipping")
    endif()
endforeach()
