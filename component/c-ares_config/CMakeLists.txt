# component/c-ares_config — the c-ares async DNS resolver (submodule component/c-ares_config/c-ares,
# pinned v1.34.6) wrapped into the `c_ares` STATIC target + `c_ares_config` INTERFACE umbrella.
# Modeled on component/littlefs_config: explicit source list (NO file(GLOB)), -w on the upstream
# sources, hand-written ares_config.h (this dir) + ares_build.h (copied from the upstream .dist,
# whose __GNUC__ branch fits us). Sockets/select come from the component/syscall_config BSD shim.
#
# Source subset = the ares_getaddrinfo + UDP/TCP + DNS-record-parse + select-loop closure. Excluded:
# the event backends (src/lib/event/*, we drive select ourselves), the legacy *_reply parsers,
# windows/mac/android platform files, and ares_getnameinfo (reverse lookup, unused).

set(_cares "${CMAKE_CURRENT_LIST_DIR}/c-ares")

# ares_build.h: the distributed default header's __GNUC__ branch already matches us (socklen_t,
# sys/socket.h, sys/select.h). Copy it into the build dir so <ares.h> finds it.
configure_file("${_cares}/include/ares_build.h.dist"
               "${CMAKE_CURRENT_BINARY_DIR}/ares_build.h" COPYONLY)

add_library(c_ares STATIC
    # ---- src/lib (top level) ----
    # (the legacy struct-hostent path — ares_addrinfo2hostent / ares_free_hostent /
    #  ares_gethostby{name,addr} — is excluded: we use the ares_getaddrinfo/ares_addrinfo path,
    #  and the shim netdb.h has no struct hostent.)
    "${_cares}/src/lib/ares_addrinfo_localhost.c"
    "${_cares}/src/lib/ares_cancel.c"
    "${_cares}/src/lib/ares_close_sockets.c"
    "${_cares}/src/lib/ares_conn.c"
    "${_cares}/src/lib/ares_cookie.c"
    "${_cares}/src/lib/ares_data.c"
    "${_cares}/src/lib/ares_destroy.c"
    "${_cares}/src/lib/ares_free_string.c"
    "${_cares}/src/lib/ares_freeaddrinfo.c"
    "${_cares}/src/lib/ares_getaddrinfo.c"
    "${_cares}/src/lib/ares_getenv.c"
    "${_cares}/src/lib/ares_hosts_file.c"
    "${_cares}/src/lib/ares_init.c"
    "${_cares}/src/lib/ares_library_init.c"
    "${_cares}/src/lib/ares_metrics.c"
    "${_cares}/src/lib/ares_options.c"
    "${_cares}/src/lib/ares_parse_into_addrinfo.c"
    "${_cares}/src/lib/ares_process.c"
    "${_cares}/src/lib/ares_qcache.c"
    "${_cares}/src/lib/ares_query.c"
    "${_cares}/src/lib/ares_search.c"
    "${_cares}/src/lib/ares_send.c"
    "${_cares}/src/lib/ares_set_socket_functions.c"
    "${_cares}/src/lib/ares_socket.c"
    "${_cares}/src/lib/ares_strerror.c"
    "${_cares}/src/lib/ares_sysconfig.c"
    "${_cares}/src/lib/ares_sysconfig_files.c"
    "${_cares}/src/lib/ares_timeout.c"
    "${_cares}/src/lib/ares_update_servers.c"
    "${_cares}/src/lib/ares_version.c"
    "${_cares}/src/lib/inet_net_pton.c"
    "${_cares}/src/lib/inet_ntop.c"
    # ---- src/lib/dsa (containers) ----
    "${_cares}/src/lib/dsa/ares_array.c"
    "${_cares}/src/lib/dsa/ares_htable.c"
    "${_cares}/src/lib/dsa/ares_htable_asvp.c"
    "${_cares}/src/lib/dsa/ares_htable_dict.c"
    "${_cares}/src/lib/dsa/ares_htable_strvp.c"
    "${_cares}/src/lib/dsa/ares_htable_szvp.c"
    "${_cares}/src/lib/dsa/ares_htable_vpstr.c"
    "${_cares}/src/lib/dsa/ares_htable_vpvp.c"
    "${_cares}/src/lib/dsa/ares_llist.c"
    "${_cares}/src/lib/dsa/ares_slist.c"
    # ---- src/lib/record (DNS wire format) ----
    "${_cares}/src/lib/record/ares_dns_mapping.c"
    "${_cares}/src/lib/record/ares_dns_multistring.c"
    "${_cares}/src/lib/record/ares_dns_name.c"
    "${_cares}/src/lib/record/ares_dns_parse.c"
    "${_cares}/src/lib/record/ares_dns_record.c"
    "${_cares}/src/lib/record/ares_dns_write.c"
    # ---- src/lib/str ----
    "${_cares}/src/lib/str/ares_buf.c"
    "${_cares}/src/lib/str/ares_str.c"
    "${_cares}/src/lib/str/ares_strsplit.c"
    # ---- src/lib/util ----
    "${_cares}/src/lib/util/ares_iface_ips.c"
    "${_cares}/src/lib/util/ares_math.c"
    "${_cares}/src/lib/util/ares_rand.c"
    "${_cares}/src/lib/util/ares_threads.c"
    "${_cares}/src/lib/util/ares_timeval.c"
    "${_cares}/src/lib/util/ares_uri.c"
    # ---- src/lib/legacy (the ares_fds / ares_getsock select-loop helpers) ----
    "${_cares}/src/lib/legacy/ares_fds.c"
    "${_cares}/src/lib/legacy/ares_getsock.c"
    # ---- at-project no-op stubs (RFC6724 sorter / .onion / event-thread; see cares_stubs.c) ----
    "${CMAKE_CURRENT_LIST_DIR}/cares_stubs.c"
)

target_compile_options(c_ares PRIVATE -w)
target_compile_definitions(c_ares PRIVATE HAVE_CONFIG_H)
target_compile_definitions(c_ares PUBLIC CARES_STATICLIB)

target_include_directories(c_ares PUBLIC
    "${_cares}/include"            # ares.h, ares_dns_record.h, ares_build.h(.dist neighbours)
    "${CMAKE_CURRENT_LIST_DIR}"    # our hand-written ares_config.h
    "${CMAKE_CURRENT_BINARY_DIR}"  # generated ares_build.h
)
target_include_directories(c_ares PRIVATE
    "${_cares}/src/lib"            # ares_private.h, ares_setup.h
    "${_cares}/src/lib/include"    # ares_buf.h, ares_array.h, ... (private)
    "${_cares}/src/lib/event"      # ares_event.h (for the stubbed event-thread signatures)
)
# BSD socket headers (<sys/socket.h>, <netinet/in.h>, <arpa/inet.h>, <netdb.h>) from the shim.
target_include_directories(c_ares SYSTEM PRIVATE
    $<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>
)

# Umbrella INTERFACE (伞形): consumers link `c_ares_config`.
add_library(c_ares_config INTERFACE)
target_link_libraries(c_ares_config INTERFACE c_ares)

# Synchronous resolver front-end (cares_resolve.c) - merged in from the former
# component/amebad/cares_port (no platform-HAL code: c-ares + a select() loop over the BSD-sockets
# shim). EXCLUDE_FROM_ALL: built only when app_km4 links it (CARES=ON). rxi_log_config is added after
# this component, so take its headers via a genex (the log_* symbols resolve at the final app link).
add_library(cares_port STATIC EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/cares_resolve.c")
target_compile_options(cares_port PRIVATE "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")
target_include_directories(cares_port PUBLIC "${CMAKE_CURRENT_LIST_DIR}")   # cares_resolve.h for consumers
target_link_libraries(cares_port PUBLIC c_ares_config)                      # propagate the c_ares archive
target_link_libraries(cares_port PRIVATE freertos_config freertos-plus-tcp_config)
target_include_directories(cares_port SYSTEM PRIVATE
    $<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>     # select/fd_set, sockaddr_in/in6
    $<TARGET_PROPERTY:rxi_log_config,INTERFACE_INCLUDE_DIRECTORIES>     # at_log.h
)
