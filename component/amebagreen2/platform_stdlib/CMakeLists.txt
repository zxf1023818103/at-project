add_library(amebagreen2_platform_stdlib_headers INTERFACE)
target_include_directories(amebagreen2_platform_stdlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
