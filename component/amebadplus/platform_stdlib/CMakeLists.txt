add_library(amebadplus_platform_stdlib_headers INTERFACE)
target_include_directories(amebadplus_platform_stdlib_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")
