# log_config — rxi's log.c (submodule component/log_config/log @ upstream), a tiny leveled logger:
# log_trace/debug/info/warn/error/fatal print "HH:MM:SS LEVEL file:line: msg" to stderr via
# fprintf. We keep that DEFAULT fprintf output and add only thread-safety — log_lock.c backs
# log.c's log_set_lock() hook with a FreeRTOS mutex so concurrent tasks don't interleave a line.
# Consumers: link `log_config`, `#include "log.h"`, and call log_lock_init() once at startup.
#
# Dependencies: freertos_config (the mutex; defined later in the amebad subtree, resolved at
# generate). The logger itself uses newlib stdio (fprintf/vfprintf) + time() for the timestamp,
# present on every platform here. No config header — log.c's compile-time options (LOG_USE_COLOR,
# MAX_CALLBACKS) are left at their defaults.
set(_log "${CMAKE_CURRENT_SOURCE_DIR}/log/src")

add_library(rxi_log STATIC EXCLUDE_FROM_ALL
    "${_log}/log.c"                          # upstream logger (default fprintf->stderr output)
    "${CMAKE_CURRENT_SOURCE_DIR}/log_lock.c" # FreeRTOS-mutex lock installed via log_set_lock()
)
# PUBLIC (SYSTEM): only this dir — at_log.h (the consumer entry point) + log_lock.h. log/src is
# PRIVATE on purpose: its header is the generic "log.h", which collides with other components'
# log.h (tlsf_config/port_generic/log.h, ...). Consumers include "at_log.h", not "log.h".
target_include_directories(rxi_log SYSTEM
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${_log}"
)
target_link_libraries(rxi_log PUBLIC freertos_config)
target_compile_options(rxi_log PRIVATE -w)   # vendor source: silence warnings under -Werror

# Umbrella INTERFACE (伞形): consumers link `log_config` to get the logger + its lock.
add_library(log_config INTERFACE)
target_link_libraries(log_config INTERFACE rxi_log)
