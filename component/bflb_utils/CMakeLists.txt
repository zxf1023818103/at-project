# bflb_utils — Bouffalo SDK utility library
# (extracted from platform/bouffalo_sdk/components/utils).
# Multi-module: log / ring_buffer / cjson / coredump / partition / mtd / cjson / math / list / etc.
# Each module is selected via SDK Kconfig flags.

set(BFLB_UTILS_SOURCES
    log/log.c
    ring_buffer/ring_buffer.c
    bflb_block_pool/bflb_block_pool.c
    bflb_timestamp/bflb_timestamp.c
    getopt/utils_getopt.c
    cjson/cJSON.c
    cjson/cJSON_Utils.c
    math/src/utils_base64.c
    math/src/utils_crc.c
    math/src/utils_hex.c
    list/utils_list.c
)

set(BFLB_UTILS_PUBLIC_INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/log
    ${CMAKE_CURRENT_LIST_DIR}/ring_buffer
    ${CMAKE_CURRENT_LIST_DIR}/bflb_block_pool
    ${CMAKE_CURRENT_LIST_DIR}/bflb_timestamp
    ${CMAKE_CURRENT_LIST_DIR}/getopt
    ${CMAKE_CURRENT_LIST_DIR}/cjson
    ${CMAKE_CURRENT_LIST_DIR}/math/include
    ${CMAKE_CURRENT_LIST_DIR}/list
)

if(CONFIG_BFLB_LOG)
    list(APPEND BFLB_UTILS_SOURCES log/bflb_log/bflb_log.c)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/log/bflb_log)
    if(CONFIG_FREERTOS)
        list(APPEND BFLB_UTILS_SOURCES log/log_freertos.c)
    endif()
endif()

if(CONFIG_COREDUMP_V2)
    list(APPEND BFLB_UTILS_SOURCES
        coredump/coredump.c
        coredump/encoder_v2.c
        coredump/bin_fmt_v2.c)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/coredump)
elseif(CONFIG_COREDUMP)
    list(APPEND BFLB_UTILS_SOURCES
        coredump/coredump.c
        coredump/encoder_v1.c
        coredump/bin_fmt_v1.c)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/coredump)
endif()

if(CONFIG_PARTITION)
    list(APPEND BFLB_UTILS_SOURCES partition/partition.c)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/partition)
endif()

if(CONFIG_BFLB_MTD)
    list(APPEND BFLB_UTILS_SOURCES bflb_mtd/bflb_mtd.c bflb_mtd/bflb_boot2.c)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/bflb_mtd/include)
endif()

if(CONFIG_FREERTOS)
    list(APPEND BFLB_UTILS_SOURCES async_event/async_event.c async_event/rtos_al.c)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/async_event)
endif()

if(CONFIG_IPC)
    if(CONFIG_RPMSG_SERVICE_MODE_MASTER)
        list(APPEND BFLB_UTILS_SOURCES
            ipc_rpmsg_ops/flash_ops_rpmsg/flash_ops_rpmsg_master.c
            ipc_rpmsg_ops/rpmsg_master_init.c)
    else()
        list(APPEND BFLB_UTILS_SOURCES
            ipc_rpmsg_ops/flash_ops_rpmsg/flash_ops_rpmsg_slave.c
            ipc_rpmsg_ops/rpmsg_slave_init.c)
    endif()
endif()

if(CONFIG_RPMSG_SERVICE)
    list(APPEND BFLB_UTILS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/ipc_rpmsg_ops/flash_ops_rpmsg)
endif()

add_library(bflb_utils STATIC ${BFLB_UTILS_SOURCES})

target_include_directories(bflb_utils PUBLIC ${BFLB_UTILS_PUBLIC_INCLUDES})

# Log level default (was -DCONFIG_LOG_LEVEL=3 in SDK CMakeLists)
if(NOT DEFINED CONFIG_LOG_LEVEL)
    target_compile_definitions(bflb_utils PUBLIC CONFIG_LOG_LEVEL=3)
endif()

if(NOT DEFINED CONFIG_TIMEZONE)
    target_compile_definitions(bflb_utils PUBLIC BFLB_TIMESTAMP_TIMEZONE=8)
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_utils PRIVATE sdk_intf_lib)
endif()
