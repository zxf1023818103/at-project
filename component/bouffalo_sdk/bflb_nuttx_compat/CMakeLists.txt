# bflb_nuttx_compat — the three NuttX-flavoured SDK compat headers that outlived
# bflb_libc. Bouffalo's WiFi OS adapter (wifi4/bflb_adapter/bflb_os_adapter/
# bflb_os_hal.c) includes <nuttx/config.h> (FAR/CODE + the lib_malloc()->kmalloc
# macros), which pulls <nuttx/compiler.h> and "nuttx_limits.h". These are genuinely
# Bouffalo-SDK glue (not libc), so when bflb_libc was deleted — its newlib retarget /
# printf / NuttX stdlib+string are replaced by newlib + component/syscall_config — these
# three headers were relocated here instead of dropped.
#
# Header-only INTERFACE. Include root is this dir so `<nuttx/config.h>` resolves to
# nuttx/config.h. On the Bouffalo umbrella include path exactly where bflb_libc's PUBLIC
# include used to sit. (bflb_libc's reent.h was a transparent `#include_next <reent.h>`
# and its sys/types.h matched the toolchain's for CONFIG_POSIX=n — both dropped to the
# toolchain newlib headers.)
add_library(bflb_nuttx_compat INTERFACE)
target_include_directories(bflb_nuttx_compat INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# nuttx/config.h does `#include "mm.h"` + defines lib_malloc(s) -> kmalloc(...): the
# mm.h header (kmalloc/krealloc/kmemalign/kfree + MM_FLAG_*) comes from tlsf_config.
target_link_libraries(bflb_nuttx_compat INTERFACE tlsf_config)
