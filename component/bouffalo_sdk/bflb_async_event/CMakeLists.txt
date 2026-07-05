# bflb_async_event — async deferred-work queue for posting tasks to a worker
# context. rtos_al.c is the FreeRTOS adapter for the SDK's rtos_al wrappers.
# Extracted from bouffalo_sdk/components/utils/async_event.

add_library(bflb_async_event STATIC
    async_event.c
    rtos_al.c
)

target_include_directories(bflb_async_event PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Both .c sources #include <FreeRTOS.h>.
target_link_libraries(bflb_async_event PUBLIC freertos_port)
