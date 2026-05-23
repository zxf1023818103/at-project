cmake_minimum_required(VERSION 3.16)
project(at_user C CXX ASM)

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/zlib.cmake)

add_library(at_user STATIC ${AT_USER_SOURCES})
target_include_directories(at_user PUBLIC ${AT_USER_SRC_DIR})
target_link_libraries(at_user PRIVATE zlibstatic)

# Walk at_user's transitive link dependencies and collect every target whose
# TYPE is STATIC_LIBRARY. The resulting absolute paths get written to
# ${CMAKE_BINARY_DIR}/user_link.list, one per line.
#
# CMake is the source of truth here: in src/CMakeLists.txt you may add
# `add_library(zlib STATIC ...)` (or `add_subdirectory(zlib)` etc.) and
# wire it in via `target_link_libraries(at_user PRIVATE zlibstatic)`, and the
# list will pick it up automatically.
#
# Consumers that don't drive linking themselves (notably the AmebaD SDK's
# Make-based final linker step) read this file at link time. For consumers
# that DO drive linking via CMake (Bouffalo / ameba-rtos new SDK), the file
# is harmless extra output and they ignore it.
function(at_collect_static_libs target out_var)
    set(_result "")
    set(_visited "")
    set(_pending "${target}")
    while(_pending)
        list(POP_FRONT _pending _cur)
        if(_cur IN_LIST _visited)
            continue()
        endif()
        list(APPEND _visited "${_cur}")
        if(NOT TARGET "${_cur}")
            continue()  # plain string like "-lm" or "pthread"
        endif()
        get_target_property(_type "${_cur}" TYPE)
        if(_type STREQUAL "STATIC_LIBRARY")
            list(APPEND _result "$<TARGET_FILE:${_cur}>")
        endif()
        get_target_property(_deps "${_cur}" LINK_LIBRARIES)
        if(_deps)
            list(APPEND _pending ${_deps})
        endif()
        get_target_property(_idep "${_cur}" INTERFACE_LINK_LIBRARIES)
        if(_idep)
            list(APPEND _pending ${_idep})
        endif()
    endwhile()
    set("${out_var}" "${_result}" PARENT_SCOPE)
endfunction()

at_collect_static_libs(at_user AT_USER_STATIC_LIBS)
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/user_link.list"
    CONTENT "$<JOIN:${AT_USER_STATIC_LIBS},\n>\n")
