include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
include(${AT_PROJECT_ROOT}/cmake/at_collect_static_libs.cmake)

add_library(at_user STATIC ${AT_USER_SOURCES})
target_include_directories(at_user PUBLIC ${AT_USER_SRC_DIR})
target_link_libraries(at_user PRIVATE zlibstatic)

# Walk at_user's transitive deps and write the resolved STATIC_LIBRARY
# .a paths to user_link.list. AmebaD's Make-driven final linker step
# reads this file at link time; bouffalo / ameba-new ignore it (they
# already link via CMake). See cmake/at_collect_static_libs.cmake for
# the resolution logic.
at_collect_static_libs(at_user AT_USER_STATIC_LIBS)
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/user_link.list"
    CONTENT "$<JOIN:${AT_USER_STATIC_LIBS},\n>\n")
