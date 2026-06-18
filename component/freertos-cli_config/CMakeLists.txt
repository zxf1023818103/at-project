# component/freertos-cli_config — FreeRTOS-Plus-CLI (the command-line interpreter:
# FreeRTOS_CLIRegisterCommand / FreeRTOS_CLIProcessCommand over a registered command table).
#
# VENDORED, not a submodule: FreeRTOS-Plus-CLI has no standalone upstream repo — the two
# source files live only inside the FreeRTOS/FreeRTOS monorepo at
# FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI/. They are copied verbatim into ./freertos-cli/
# (FreeRTOS V202212.00, MIT — see the per-file header). The upstream layout is preserved so
# a later switch to a real submodule at the same path is a drop-in. Like littlefs_config /
# freertos-posix_config this is a hand wrapper — there is no upstream CMake to delegate to.
#
# No config header: unlike coreHTTP/coreMQTT, FreeRTOS-Plus-CLI takes all of its knobs
# (configCOMMAND_INT_MAX_OUTPUT_SIZE — REQUIRED, no fallback; configINCLUDE_QUERY_HEADING_COMMAND;
# configAPPLICATION_PROVIDES_cOutputBuffer) straight from the platform FreeRTOSConfig.h, which
# arrives transitively via the `freertos_config` link below.
#
# Cross-platform: no chip gate — it rides whatever FreeRTOS kernel is built (links the
# `FreeRTOS` alias + freertos_config). EXCLUDE_FROM_ALL until a consumer links it.
#
# Targets produced:
#   freertos_cli         STATIC — FreeRTOS_CLI.c.
#   freertos-cli_config  INTERFACE umbrella over it.
set(_p "${CMAKE_CURRENT_SOURCE_DIR}/freertos-cli")

add_library(freertos_cli STATIC EXCLUDE_FROM_ALL
    "${_p}/FreeRTOS_CLI.c"
)

# PUBLIC (SYSTEM, to keep the vendor header out of -Werror): <FreeRTOS_CLI.h>.
target_include_directories(freertos_cli SYSTEM PUBLIC
    "${_p}"   # FreeRTOS_CLI.h
)

# FreeRTOS kernel (tasks/list) + FreeRTOSConfig.h (configCOMMAND_INT_MAX_OUTPUT_SIZE etc.).
target_link_libraries(freertos_cli PUBLIC FreeRTOS freertos_config)
target_compile_options(freertos_cli PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `freertos-cli_config` to get the interpreter.
add_library(freertos-cli_config INTERFACE)
target_link_libraries(freertos-cli_config INTERFACE freertos_cli)
