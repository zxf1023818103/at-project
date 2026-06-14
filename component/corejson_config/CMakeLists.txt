# corejson_config — FreeRTOS coreJSON (submodule component/corejson_config/corejson @ v3.3.1),
# a strict, parser-only JSON library (no allocation, no DOM). Transitive dependency of the AWS
# IoT libraries: jobs / ota parse JSON documents with JSON_Search etc. NOTE this is coreJSON,
# distinct from the DaveGamble cJSON already vendored at component/cjson_config (different API).
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `core_json` target),
# like coremqtt_config / backoffalgorithm_config — not a hand-maintained file list. No config
# header / logging facade: coreJSON is a pure parser. Cross-platform: no chip gate, no dependency.

# Upstream add_library(core_json ${JSON_SOURCES}) has no explicit type -> follows
# BUILD_SHARED_LIBS; pin it static BEFORE add_subdirectory. EXCLUDE_FROM_ALL: built only when linked.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(corejson "${CMAKE_CURRENT_BINARY_DIR}/upstream" EXCLUDE_FROM_ALL)

# core_json's PUBLIC include path (source/include, with core_json.h) is set by upstream; just
# silence the vendor source warnings.
target_compile_options(core_json PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `corejson_config` to get core_json.
add_library(corejson_config INTERFACE)
target_link_libraries(corejson_config INTERFACE core_json)
