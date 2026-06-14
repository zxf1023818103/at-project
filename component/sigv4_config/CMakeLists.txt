# sigv4_config — AWS SigV4 (submodule component/sigv4_config/sigv4 @ v1.3.1): generates the
# AWS Signature Version 4 `Authorization` header / query string for signing AWS IoT (and other
# AWS) HTTP requests. Pairs with corehttp_config.
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `sigv4` target, from
# sigv4FilePaths.cmake), like coremqtt_config — not a hand-maintained file list. The project's
# sigv4_config.h (this dir) is put on sigv4's include path; `sigv4_config` is the INTERFACE
# umbrella consumers link.
#
# No crypto dependency: the consumer supplies SHA-256 through SigV4CryptoInterface_t (a function
# table), so SigV4 itself links nothing. Cross-platform: no chip gate.
add_subdirectory(sigv4 "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# sigv4.c pulls sigv4_config_defaults.h -> sigv4_config.h: put our config header on the target's
# path (PUBLIC so consumers of <sigv4.h> see it too). SYSTEM keeps it out of -Werror; -w silences
# the vendor sources.
target_include_directories(sigv4 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(sigv4 PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `sigv4_config` to get SigV4.
add_library(sigv4_config INTERFACE)
target_link_libraries(sigv4_config INTERFACE sigv4)
