# component/amebad/build_info — replaces the SDK's generated inc_hp/build_info.h
# (UTS_VERSION + RTL_FW_COMPILE_* banner macros) with compile-time -D, so there is
# no generated header on the include path.
#
# Mirrors the surface of the SDK Makefile's build_info.h recipe
# (date / id -u -n / hostname / dnsdomainname / CC -v), captured ONCE at CMake
# configure time and injected as -D. A single timestamp — NOT the compiler's
# per-TU __DATE__/__TIME__ — so every place that references these macros sees the
# exact same value (globally unified across all consumers / translation units).
# Re-runs on reconfigure (CMakeLists change / fresh build dir).
#
# Only UTS_VERSION is actually referenced today (hci_board.c "BT BUILD Date: %s",
# which now shows the build timestamp); the rest are defined to preserve the
# original SDK macro surface for the vendored BT source.
#
# Consumers `target_link_libraries(<tgt> ... build_info)` instead of including a
# header. INTERFACE-only (no source, no chip gate): it is purely the macros.

# SDK format: UTS_VERSION "2026/06/07-08:31:13", RTL_FW_COMPILE_DATE "2026/06/07".
string(TIMESTAMP _bi_datetime "%Y/%m/%d-%H:%M:%S")
string(TIMESTAMP _bi_date     "%Y/%m/%d")

cmake_host_system_information(RESULT _bi_host QUERY HOSTNAME)

if(DEFINED ENV{USER})
    set(_bi_by "$ENV{USER}")
elseif(DEFINED ENV{USERNAME})         # Windows
    set(_bi_by "$ENV{USERNAME}")
else()
    set(_bi_by "unknown")
endif()

# Domain: best-effort, matching the SDK's dnsdomainname/domainname probe. Empty
# (like the SDK default) when neither resolves.
execute_process(COMMAND dnsdomainname
    OUTPUT_VARIABLE _bi_domain OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_bi_domain STREQUAL "" OR _bi_domain MATCHES "not found|Name or service")
    set(_bi_domain "")
endif()

add_library(build_info INTERFACE)
target_compile_definitions(build_info INTERFACE
    # one configure-time timestamp, identical for every consumer.
    "UTS_VERSION=\"${_bi_datetime}\""
    "RTL_FW_COMPILE_TIME=\"${_bi_datetime}\""
    "RTL_FW_COMPILE_DATE=\"${_bi_date}\""
    # build environment — captured at configure.
    "RTL_FW_COMPILE_BY=\"${_bi_by}\""
    "RTL_FW_COMPILE_HOST=\"${_bi_host}\""
    "RTL_FW_COMPILE_DOMAIN=\"${_bi_domain}\""
    "RTL_FW_COMPILER=\"${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\"")
