# bflb_shell — extracted from platform/bouffalo_sdk/components/shell.
# letter-shell interpreter; the freertos backend is gated by SDK Kconfig.

set(BFLB_SHELL_SOURCES shell.c)

if(CONFIG_FREERTOS)
    list(APPEND BFLB_SHELL_SOURCES shell_freertos.c)
endif()

add_library(bflb_shell STATIC ${BFLB_SHELL_SOURCES})

target_include_directories(bflb_shell PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(CONFIG_SHELL_CMD_SIZE)
    target_compile_definitions(bflb_shell PUBLIC SHELL_CMD_SIZE=${CONFIG_SHELL_CMD_SIZE})
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_shell PRIVATE sdk_intf_lib)
endif()
