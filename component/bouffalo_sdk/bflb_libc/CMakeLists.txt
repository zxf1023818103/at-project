# bflb_libc — Bouffalo's libc shim: newlib syscalls + NuttX stdlib/string
# borrows + their own printf/snprintf family. Extracted from
# bouffalo_sdk/components/libc/.
#
# What's here:
#   newlib/    syscall stubs (sbrk / write / read / time / fstat / ...)
#              and three filesystem backends:
#                port_file_nosys.c   no FS (default)
#                port_file_littlefs.c  via component/thirdparty/littlefs
#                port_file_fatfs.c     via FATFS (not in this tree yet)
#   nuttx/     trimmed NuttX libc — stdlib (atoi/atof/qsort/itoa/…) +
#              string (memmove/memset/strchr/strdup/…). Used because the
#              toolchain's bundled newlib equivalents are bigger / slower.
#   printf.c + family  Bouffalo's printf (calls into the picked vsnprintf
#                      backend).
#   vsnprintf.c        Full %f / %lld vsnprintf (picked when CONFIG_VSNPRINTF_*
#                      flags are on — the project's proj.conf has them).
#   vsnprintf_nano.c   Tiny vsnprintf (picked when CONFIG_VSNPRINTF_NANO=y).
#   assert.c           __assert_func.
#   stdatomic.c        C11 atomic helpers — opt-in via CONFIG_STDATOMIC.
#
# Build options (default to "match proj.conf today" — flip via -D as needed):
#   BFLB_LIBC_FS                "nosys" | "littlefs" | "fatfs"  (default nosys)
#   BFLB_LIBC_VSNPRINTF_NANO    OFF                              (use full vsnprintf)
#   BFLB_LIBC_FLOAT             ON   (-DCONFIG_LIBC_FLOAT=1)
#   BFLB_LIBC_FLOAT_EX          ON   (-DCONFIG_LIBC_FLOAT_EX=1)
#   BFLB_LIBC_LONG_LONG         ON   (-DCONFIG_LIBC_LONG_LONG=1)
#   BFLB_LIBC_SSCANF            OFF  (build sscanf + -Wl,-wrap,sscanf)
#   BFLB_LIBC_STDATOMIC         OFF  (build stdatomic.c)
#
# Critical link-time piece: snprintf/sprintf are -Wl,-wrap'd so newlib's
# implementations never end up in the link — ours do. Those link options
# are exposed via the bflb_libc INTERFACE_LINK_OPTIONS so consumers (or
# the umbrella) pick them up automatically.

set(BFLB_LIBC_FS "nosys" CACHE STRING "Filesystem backend: nosys | littlefs | fatfs")
set_property(CACHE BFLB_LIBC_FS PROPERTY STRINGS nosys littlefs fatfs)

option(BFLB_LIBC_VSNPRINTF_NANO "Use vsnprintf_nano.c instead of vsnprintf.c" OFF)
option(BFLB_LIBC_FLOAT          "Enable %f/%F in vsnprintf"                   ON)
option(BFLB_LIBC_FLOAT_EX       "Enable %g/%G/%e/%E in vsnprintf"             ON)
option(BFLB_LIBC_LONG_LONG      "Enable %lld/%llu/… in vsnprintf"             ON)
option(BFLB_LIBC_SSCANF         "Build sscanf wrapper"                        OFF)
option(BFLB_LIBC_STDATOMIC      "Build stdatomic.c"                           OFF)

add_library(bflb_libc STATIC
    # newlib glue — always-on bits
    newlib/syscalls_simple_io.c
    newlib/port_memory.c
    newlib/port_time.c
    newlib/port_init_fini.c
    # Bouffalo printf family
    snprintf.c
    sprintf.c
    vsprintf.c
    printf.c
    vprintf.c
    assert.c
    # NuttX stdlib
    nuttx/libc/stdlib/lib_abs.c
    nuttx/libc/stdlib/lib_atof.c
    nuttx/libc/stdlib/lib_atoi.c
    nuttx/libc/stdlib/lib_atol.c
    nuttx/libc/stdlib/lib_atoll.c
    nuttx/libc/stdlib/lib_bsearch.c
    nuttx/libc/stdlib/lib_checkbase.c
    nuttx/libc/stdlib/lib_itoa.c
    nuttx/libc/stdlib/lib_llabs.c
    nuttx/libc/stdlib/lib_lldiv.c
    nuttx/libc/stdlib/lib_qsort.c
    nuttx/libc/stdlib/lib_strtod.c
    # NuttX string
    nuttx/libc/string/lib_ffs.c
    nuttx/libc/string/lib_ffsl.c
    nuttx/libc/string/lib_ffsll.c
    nuttx/libc/string/lib_fls.c
    nuttx/libc/string/lib_flsl.c
    nuttx/libc/string/lib_flsll.c
    nuttx/libc/string/lib_index.c
    nuttx/libc/string/lib_memccpy.c
    nuttx/libc/string/lib_memchr.c
    nuttx/libc/string/lib_memcmp.c
    nuttx/libc/string/lib_memmove.c
    nuttx/libc/string/lib_memrchr.c
    nuttx/libc/string/lib_memset.c
    nuttx/libc/string/lib_stpcpy.c
    nuttx/libc/string/lib_stpncpy.c
    nuttx/libc/string/lib_strcasecmp.c
    nuttx/libc/string/lib_strcasestr.c
    nuttx/libc/string/lib_strcat.c
    nuttx/libc/string/lib_strchr.c
    nuttx/libc/string/lib_strcmp.c
    nuttx/libc/string/lib_strcspn.c
    nuttx/libc/string/lib_strdup.c
    nuttx/libc/string/lib_strnlen.c
    nuttx/libc/string/lib_strpbrk.c
    nuttx/libc/string/lib_strrchr.c
    nuttx/libc/string/lib_strsep.c
    nuttx/libc/string/lib_strspn.c
    nuttx/libc/string/lib_strstr.c
    nuttx/libc/string/lib_strtok.c
    nuttx/libc/string/lib_strtokr.c
    nuttx/libc/string/lib_vikmemcpy.c
)

# Filesystem backend.
if("${BFLB_LIBC_FS}" STREQUAL "nosys")
    target_sources(bflb_libc PRIVATE
        newlib/port_file_nosys.c
        newlib/syscalls_nosys.c
    )
elseif("${BFLB_LIBC_FS}" STREQUAL "littlefs")
    target_sources(bflb_libc PRIVATE
        newlib/syscalls.c
        newlib/port_tty.c
        newlib/port_file_littlefs.c
    )
    target_link_libraries(bflb_libc PUBLIC littlefs)
elseif("${BFLB_LIBC_FS}" STREQUAL "fatfs")
    target_sources(bflb_libc PRIVATE
        newlib/syscalls.c
        newlib/port_tty.c
        newlib/port_file_fatfs.c
    )
    message(WARNING "bflb_libc: BFLB_LIBC_FS=fatfs but no FATFS component extracted yet")
else()
    message(FATAL_ERROR "bflb_libc: BFLB_LIBC_FS must be nosys|littlefs|fatfs (got '${BFLB_LIBC_FS}')")
endif()

# vsnprintf flavor.
if(BFLB_LIBC_VSNPRINTF_NANO)
    target_sources(bflb_libc PRIVATE vsnprintf_nano.c)
else()
    target_sources(bflb_libc PRIVATE vsnprintf.c)
endif()

# vsnprintf feature flags.
if(BFLB_LIBC_FLOAT)
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_FLOAT=1)
else()
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_FLOAT=0)
endif()
if(BFLB_LIBC_FLOAT_EX)
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_FLOAT_EX=1)
endif()
if(BFLB_LIBC_LONG_LONG)
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_LONG_LONG=1)
endif()

if(BFLB_LIBC_SSCANF)
    target_sources(bflb_libc PRIVATE sscanf.c)
    target_link_options(bflb_libc INTERFACE -Wl,-wrap,sscanf)
endif()

if(BFLB_LIBC_STDATOMIC)
    target_sources(bflb_libc PRIVATE stdatomic.c)
endif()

# Bouffalo's printf/snprintf/sprintf are linked via --wrap so they
# override newlib's. INTERFACE-level link options propagate to the final
# ELF.
target_link_options(bflb_libc INTERFACE
    -Wl,-wrap,snprintf
    -Wl,-wrap,sprintf
)

# Public include surface — sys/ for type aliases, nuttx/ for compiler.h
# and nuttx_limits.h, root for reent.h.
target_include_directories(bflb_libc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/nuttx"
)

# nuttx/libc/* and Bouffalo printf use -fno-builtin so the toolchain
# doesn't replace our overrides with its builtins.
target_compile_options(bflb_libc PRIVATE -fno-builtin)

# Deps: FreeRTOS for the reentrancy hooks, bflb_lhal for uart so
# syscalls_simple_io can use it for stdout, bflb_mm for the allocator
# (port_memory.c → mm_malloc / mm_free), at_console for the shared fd 0/1/2
# dispatcher (console.h + at_console_read/write; syscalls_simple_io provides the
# bflb_uart backend via at_console_default_backend).
target_link_libraries(bflb_libc PUBLIC
    freertos_kernel
    bflb_lhal
    bflb_mm
    at_console
)

# Upstream nuttx-derived sources trip our -Wall -Wundef wall on details
# we can't reasonably patch. Same -w treatment as cjson / mbedtls.
target_compile_options(bflb_libc PRIVATE -w)
