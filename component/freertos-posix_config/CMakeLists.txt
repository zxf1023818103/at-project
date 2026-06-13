# component/freertos-posix_config — FreeRTOS-Plus-POSIX (the POSIX compatibility layer:
# pthread / mqueue / semaphore / timer / clock / sched / unistd over the FreeRTOS kernel).
# Submodule: component/freertos-posix_config/freertos-posix (FreeRTOS Labs POSIX).
#
# The submodule ships no CMakeLists, so this is a hand wrapper (like littlefs_config) — it
# is NOT add_subdirectory'd. The project's FreeRTOS_POSIX_portable.h (this dir) is the port
# config; FreeRTOS_POSIX.h includes it plus the shipped FreeRTOS_POSIX_portable_default.h.
#
# Cross-platform: no chip gate — the POSIX layer rides whatever FreeRTOS kernel is built
# (links the `FreeRTOS` alias + freertos_config for FreeRTOSConfig.h). EXCLUDE_FROM_ALL
# until a consumer links it. NB: FreeRTOS-Plus-POSIX needs configUSE_POSIX_ERRNO +
# thread-local-storage in the platform FreeRTOSConfig.h when actually used.
#
# Targets produced:
#   freertos_posix         STATIC — the 11 FreeRTOS_POSIX_*.c sources.
#   freertos-posix_config  INTERFACE umbrella over it.
set(_p "${CMAKE_CURRENT_SOURCE_DIR}/freertos-posix/FreeRTOS-Plus-POSIX")

add_library(freertos_posix STATIC EXCLUDE_FROM_ALL
    "${_p}/source/FreeRTOS_POSIX_clock.c"
    "${_p}/source/FreeRTOS_POSIX_mqueue.c"
    "${_p}/source/FreeRTOS_POSIX_pthread.c"
    "${_p}/source/FreeRTOS_POSIX_pthread_barrier.c"
    "${_p}/source/FreeRTOS_POSIX_pthread_cond.c"
    "${_p}/source/FreeRTOS_POSIX_pthread_mutex.c"
    "${_p}/source/FreeRTOS_POSIX_sched.c"
    "${_p}/source/FreeRTOS_POSIX_semaphore.c"
    "${_p}/source/FreeRTOS_POSIX_timer.c"
    "${_p}/source/FreeRTOS_POSIX_unistd.c"
    "${_p}/source/FreeRTOS_POSIX_utils.c"
)

# PUBLIC (SYSTEM, to keep the vendor headers out of -Werror): the POSIX API headers
# (<FreeRTOS_POSIX/pthread.h>, <private/...>), the layer's own headers + portable default,
# and our FreeRTOS_POSIX_portable.h.
target_include_directories(freertos_posix SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos-posix/include"          # <FreeRTOS_POSIX/...>
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos-posix/include/private"  # "iot_doubly_linked_list.h"
    "${_p}/include"                                               # FreeRTOS_POSIX.h, _types.h, _internal.h
    "${_p}/include/portable"                                      # FreeRTOS_POSIX_portable_default.h
    "${CMAKE_CURRENT_SOURCE_DIR}"                                 # our FreeRTOS_POSIX_portable.h
)

# FreeRTOS kernel (tasks/queues/semaphores/timers/event groups) + FreeRTOSConfig.h.
target_link_libraries(freertos_posix PUBLIC FreeRTOS freertos_config)
target_compile_options(freertos_posix PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `freertos-posix_config` to get the layer.
add_library(freertos-posix_config INTERFACE)
target_link_libraries(freertos-posix_config INTERFACE freertos_posix)
