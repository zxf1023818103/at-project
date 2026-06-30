# bflb_queue — header-only BSD-style sys/queue.h port (SLIST/STAILQ/TAILQ).
# Extracted from bouffalo_sdk/components/utils/queue. INTERFACE-only,
# no compiled sources.

add_library(bflb_queue INTERFACE)

target_include_directories(bflb_queue INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
