# bflb_sdk_version — validates PROJECT_SDK_VERSION and embeds it into the
# final ELF under the `.version.sdk` section so flashing/OTA tools can read
# it back. Extracted from bouffalo_sdk/bsp/CMakeLists.txt (the SDK had this
# inline with add_subdirectory(board)/add_subdirectory(common) — only the
# version-bookkeeping bit lives here; the board logic is already covered by
# bl<chip>_board_config/).
#
# Consumers must define PROJECT_SDK_VERSION (e.g. "2.3.26") before pulling
# this component in via add_subdirectory.
#
# SDK -> native CMake mapping:
#   sdk_add_compile_definitions(-DAPP_VER_X=...) -> target_compile_definitions(... PUBLIC ...)
#   target_sources(app PRIVATE sdk_version.c)    -> add_library(... STATIC sdk_version.c)

if(NOT DEFINED PROJECT_SDK_VERSION)
    message(FATAL_ERROR
        "bflb_sdk_version: PROJECT_SDK_VERSION not set "
        "(pass -DPROJECT_SDK_VERSION=x.y.z)")
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _matched "${PROJECT_SDK_VERSION}")
if(NOT _matched)
    message(FATAL_ERROR "Invalid PROJECT_SDK_VERSION '${PROJECT_SDK_VERSION}': must be x.y.z")
endif()

string(REGEX MATCHALL "[0-9]+" _ver_list "${_matched}")
list(GET _ver_list 0 _X)
list(GET _ver_list 1 _Y)
list(GET _ver_list 2 _Z)
if(NOT (_X LESS 255 AND _Y LESS 255 AND _Z LESS 255))
    message(FATAL_ERROR "Invalid PROJECT_SDK_VERSION: each component must be < 255")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/sdk_version.h"
    @ONLY
)

# OBJECT library, not STATIC: version_sdk is `static const char *` with internal
# linkage, kept alive only by `__attribute__((used, section(".version.sdk")))`
# and the linker script's KEEP(*(.version.*)). Inside a STATIC archive, no
# external symbol references this .o, so the archive contributes nothing and
# the .version.sdk section never reaches the final ELF. OBJECT bypasses the
# archive layer — every .o lands in the consumer's link directly.
add_library(bflb_sdk_version OBJECT sdk_version.c)

target_include_directories(bflb_sdk_version PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(bflb_sdk_version PUBLIC
    APP_VER_X=${_X}
    APP_VER_Y=${_Y}
    APP_VER_Z=${_Z}
)

if(DEFINED CONFIG_OTA_VERSION_PREFIX)
    target_compile_definitions(bflb_sdk_version PUBLIC
        "CONFIG_OTA_VERSION_PREFIX=\"${CONFIG_OTA_VERSION_PREFIX}\""
    )
endif()
