# tinycbor_config — Intel TinyCBOR (submodule component/tinycbor_config/tinycbor @ v7.0), a
# small CBOR (RFC 8949) encoder/decoder. Used by AWS-IoT-style FreeRTOS libraries (Device
# Defender, Jobs, OTA, fleet provisioning) and handy as a compact wire format generally.
#
# Built via the submodule's OWN upstream CMake (add_subdirectory -> the `tinycbor` target),
# like cjson_config / coremqtt_config — so a submodule bump needs no edits here. tinycbor's
# CMake defaults are hostile to a bare-metal cross build (host tools, CBOR<->JSON over
# open_memstream, install/export, examples/tests), so we FORCE them off BEFORE add_subdirectory.
# The `project(tinycbor C CXX)` it declares costs nothing: the top-level project is already
# C/CXX/ASM. Its generate_export_header()/configure_file() produce tinycbor-export.h /
# tinycbor-version.h into the build dir and put them on the target's include path — so no
# hand-written generated headers are needed.
#
# Cross-platform: no chip gate — pure C, no platform/OS dependency.
#
# Targets produced:
#   tinycbor         STATIC (from upstream) — the encoder/decoder.
#   tinycbor_config  INTERFACE umbrella over it.

# Pin the upstream options BEFORE add_subdirectory (FORCE: override its option() defaults).
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)   # static; also sets CBOR_STATIC_DEFINE -> empty CBOR_API
set(WITH_FLOATING_POINT  ON  CACHE BOOL "" FORCE)   # tinycbor's own half/float codec (no libm needed)
set(WITH_FREESTANDING    OFF CACHE BOOL "" FORCE)   # ON would push -ffreestanding PUBLIC onto consumers
set(WITH_CBOR2JSON       OFF CACHE BOOL "" FORCE)   # default ON -> cbortojson.c + open_memstream (host stdio)
set(BUILD_TOOLS          OFF CACHE BOOL "" FORCE)   # default ON -> add_subdirectory(tools): host executables
set(BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING        OFF CACHE BOOL "" FORCE)   # don't add_subdirectory(tests)
add_subdirectory(tinycbor EXCLUDE_FROM_ALL)         # built only when linked

# Silence the vendor sources: upstream attaches -Wall -Wextra -Werror=... PRIVATE to tinycbor;
# a trailing -w (same pattern as ngtcp2_static) wins on the command line and defuses -Werror=.
target_compile_options(tinycbor PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `tinycbor_config` to get TinyCBOR (cbor.h is on
# tinycbor's PUBLIC include path: its src/ + the build dir with the generated headers).
add_library(tinycbor_config INTERFACE)
target_link_libraries(tinycbor_config INTERFACE tinycbor)
