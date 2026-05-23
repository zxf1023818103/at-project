# bflb_mm — Bouffalo Lab Memory Management Framework
# (extracted from platform/bouffalo_sdk/components/mm).
# Two allocator backends: TLSF (default) and heap5; toggled by SDK Kconfig.

set(BFLB_MM_SOURCES
    mm.c
    mm_debug.c
)

set(BFLB_MM_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR})

if(NOT DEFINED CONFIG_MEM_TLSF_EN)
    set(CONFIG_MEM_TLSF_EN y)
endif()

if(CONFIG_MEM_TLSF_EN)
    list(APPEND BFLB_MM_SOURCES tlsf/tlsf.c tlsf/tlsf_allocator.c)
    list(APPEND BFLB_MM_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/tlsf)
endif()

if(CONFIG_MEM_HEAP5_EN)
    list(APPEND BFLB_MM_SOURCES heap5/heap5.c heap5/heap5_allocator.c)
    list(APPEND BFLB_MM_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/heap5)
endif()

add_library(bflb_mm STATIC ${BFLB_MM_SOURCES})

target_include_directories(bflb_mm PUBLIC ${BFLB_MM_PUBLIC_INCLUDES})

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_mm PRIVATE sdk_intf_lib)
endif()
