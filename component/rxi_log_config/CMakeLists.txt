# rxi_log_config — rxi's log.c (submodule component/rxi_log_config/log @ upstream), a tiny leveled
# logger: log_trace/debug/info/warn/error/fatal print to stderr via fprintf. We keep that default
# output and add only thread-safety + a "no log.c timestamp" reformat — log_lock.c backs log.c's
# log_set_lock() hook with a FreeRTOS mutex (so concurrent tasks don't interleave a line) and, in
# log_lock_init(), drops log.c's own "HH:MM:SS" so the console's "[HH:MM:SS.mmm]" is the only stamp.
# Consumers: link `rxi_log_config`, `#include "at_log.h"` (or "log.h"), call log_lock_init() once.
#
# Dependencies: freertos_config (the mutex; defined later in the amebad subtree, resolved at
# generate). The logger itself uses newlib stdio (fprintf/vfprintf) + time() for the timestamp,
# present on every platform here. No config header — log.c's compile-time options (LOG_USE_COLOR,
# MAX_CALLBACKS) are left at their defaults.
set(_log "${CMAKE_CURRENT_SOURCE_DIR}/log/src")

add_library(rxi_log STATIC EXCLUDE_FROM_ALL
    "${_log}/log.c"                          # upstream logger (default fprintf->stderr output)
    "${CMAKE_CURRENT_SOURCE_DIR}/log_lock.c" # FreeRTOS-mutex lock + the no-timestamp reformat
)
# PUBLIC (SYSTEM): this dir (at_log.h + log_lock.h) and log/src (log.h). log/src can be PUBLIC now
# that tlsf_config's shadowing log.h is gone, so consumers may include "log.h" (e.g. via at_log.h).
target_include_directories(rxi_log SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_log}"
)
target_link_libraries(rxi_log PUBLIC freertos_config)
target_compile_options(rxi_log PRIVATE -w)   # vendor source: silence warnings under -Werror

# Umbrella INTERFACE (伞形): consumers link `rxi_log_config` to get the logger + its lock.
add_library(rxi_log_config INTERFACE)
target_link_libraries(rxi_log_config INTERFACE rxi_log)
