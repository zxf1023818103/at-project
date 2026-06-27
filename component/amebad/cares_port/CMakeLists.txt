# component/amebad/cares_port — synchronous c-ares resolver front-end (cares_resolve), driven by a
# select() loop over the BSD-sockets shim. Compiled only when app_km4 links it (CARES=ON).

# __FILE__ -> bare basename in logs (cares_resolve.c, not component/amebad/cares_port/...).
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(cares_port STATIC
    cares_resolve.c
)

target_include_directories(cares_port PUBLIC "${CMAKE_CURRENT_LIST_DIR}")

# c_ares (PUBLIC so app_km4 pulls the archive); FreeRTOS + FreeRTOS+TCP headers (sockaddr/endpoint/
# mutex); BSD socket headers (select/fd_set, sockaddr_in/in6) from the syscall_config shim.
target_link_libraries(cares_port PUBLIC c_ares_config)
target_link_libraries(cares_port PRIVATE freertos_config freertos-plus-tcp_config rxi_log_config)
target_include_directories(cares_port SYSTEM PRIVATE
    $<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>
)
