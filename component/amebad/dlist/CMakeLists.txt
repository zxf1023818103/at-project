# component/amebad/dlist — the Realtek doubly-linked-list header (struct list_head
# + the list_add/list_del/list_for_each helpers), split out of the old
# sdk_headers/platform/ bundle into its own header-only component. Consumed via
# `#include "dlist.h"` by wlan / at_cmd / freertos_port, reached through
# amebad_sdk_includes (which links this). Header-only -> INTERFACE, not chip-gated.
add_library(amebad_dlist INTERFACE)
target_include_directories(amebad_dlist INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
