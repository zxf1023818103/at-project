# bflb_libc — Bouffalo SDK libc port
# (extracted from platform/bouffalo_sdk/components/libc).
# Mix of newlib syscall stubs, picolibc ports, printf/scanf family, and
# nuttx-derived stdlib/string. SDK Kconfig flags select which pieces.

set(BFLB_LIBC_SOURCES
    snprintf.c
    sprintf.c
    vsprintf.c
    printf.c
    vprintf.c
    assert.c
    newlib/syscalls_simple_io.c
    newlib/port_memory.c
    newlib/port_time.c
    newlib/port_init_fini.c
)

set(BFLB_LIBC_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR})

# Picolibc port for BL618DG b0
if("${CHIP}" STREQUAL "bl618dg" AND "${CPU_MODEL}" STREQUAL "b0")
    list(APPEND BFLB_LIBC_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/picolibc)
    list(APPEND BFLB_LIBC_SOURCES
        picolibc/port_errno.c
        picolibc/port_libcpp.c
        picolibc/port_random.c
        picolibc/stdio_streams.c)
endif()

# File / TTY backend select
if(CONFIG_NEWLIB_LITTLEFS)
    list(APPEND BFLB_LIBC_SOURCES newlib/syscalls.c newlib/port_tty.c newlib/port_file_littlefs.c)
elseif(CONFIG_NEWLIB_FATFS)
    list(APPEND BFLB_LIBC_SOURCES newlib/syscalls.c newlib/port_tty.c newlib/port_file_fatfs.c)
else()
    list(APPEND BFLB_LIBC_SOURCES newlib/port_file_nosys.c newlib/syscalls_nosys.c)
endif()

# vsnprintf flavor
if(CONFIG_VSNPRINTF_NANO)
    list(APPEND BFLB_LIBC_SOURCES vsnprintf_nano.c)
else()
    list(APPEND BFLB_LIBC_SOURCES vsnprintf.c)
endif()

if(CONFIG_STACK_SWITCH_CALL_SSCANF)
    list(APPEND BFLB_LIBC_SOURCES sscanf.c)
endif()

# Nuttx stdlib
list(APPEND BFLB_LIBC_SOURCES
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
)

# Nuttx string
list(APPEND BFLB_LIBC_SOURCES
    nuttx/libc/string/lib_ffs.c
    nuttx/libc/string/lib_ffsl.c
    nuttx/libc/string/lib_ffsll.c
    nuttx/libc/string/lib_fls.c
    nuttx/libc/string/lib_flsl.c
    nuttx/libc/string/lib_flsll.c
    nuttx/libc/string/lib_index.c
    nuttx/libc/string/lib_memccpy.c
    nuttx/libc/string/lib_memchr.c
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
)

if(NOT CONFIG_BOOT2)
    list(APPEND BFLB_LIBC_SOURCES
        nuttx/libc/string/lib_memcmp.c
        nuttx/libc/string/lib_vikmemcpy.c)
endif()

if(CONFIG_STDATOMIC)
    list(APPEND BFLB_LIBC_SOURCES stdatomic.c)
endif()

add_library(bflb_libc STATIC ${BFLB_LIBC_SOURCES})

target_include_directories(bflb_libc PUBLIC ${BFLB_LIBC_PUBLIC_INCLUDES})

# Compile defs for printf flavor
if(CONFIG_VSNPRINTF_FLOAT)
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_FLOAT=1)
else()
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_FLOAT=0)
endif()
if(CONFIG_VSNPRINTF_FLOAT_EX)
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_FLOAT_EX=1)
endif()
if(CONFIG_VSNPRINTF_LONG_LONG)
    target_compile_definitions(bflb_libc PUBLIC CONFIG_LIBC_LONG_LONG=1)
endif()

target_compile_options(bflb_libc PRIVATE -fno-builtin)
if("${CHIP}" STREQUAL "bl618dg" AND "${CPU_MODEL}" STREQUAL "b0")
    target_compile_options(bflb_libc PRIVATE -Wno-error=nonnull-compare)
    target_compile_definitions(bflb_libc PRIVATE __PICOLIBC_ERRNO_FUNCTION=__errno)
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_libc PRIVATE sdk_intf_lib)
endif()
