# backoffalgorithm_config — FreeRTOS backoffAlgorithm (submodule
# component/backoffalgorithm_config/backoffalgorithm @ v1.4.2). Computes truncated binary
# exponential backoff-with-jitter intervals for retrying failed network ops — the standard
# companion to coreMQTT / coreHTTP / coreSNTP reconnect loops.
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `backoff_algorithm`
# target, carrying BACKOFF_ALGORITHM_SOURCES), like coremqtt_config — not a hand-maintained
# file list. The project's only override is pinning a static build and silencing vendor
# warnings; `backoffalgorithm_config` is the INTERFACE umbrella consumers link.
#
# No config header / logging facade: the library is a single pure function
# (BackoffAlgorithm_GetNextBackoff) — the consumer supplies the random value, so there is
# nothing to configure. Cross-platform: no chip gate, no dependency.

# Upstream add_library(backoff_algorithm) has no explicit type -> follows BUILD_SHARED_LIBS;
# pin it static BEFORE add_subdirectory. EXCLUDE_FROM_ALL: built only when linked.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(backoffalgorithm "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# backoff_algorithm's PUBLIC include path (source/include, with backoff_algorithm.h) is set by
# upstream; just silence the vendor source warnings.
target_compile_options(backoff_algorithm PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `backoffalgorithm_config` to get backoff_algorithm.
add_library(backoffalgorithm_config INTERFACE)
target_link_libraries(backoffalgorithm_config INTERFACE backoff_algorithm)
