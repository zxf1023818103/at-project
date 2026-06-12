# corehttp_config — FreeRTOS coreHTTP (component/corehttp_config/corehttp, v3.1.3) built as a
# cross-platform static lib, with the project's core_http_config.h (this dir) on the
# include path. Mirrors the wolfssl_config / ngtcp2_config arrangement: the third-party
# source stays untouched under thirdparty/, the build glue + config live here.
#
# Cross-platform: no chip gate. coreHTTP is transport-agnostic — the consumer supplies
# the TransportInterface_t — so the library itself has no platform dependency. The HTTP
# parser is the bundled llhttp nested submodule
# (component/corehttp_config/corehttp/source/dependency/3rdparty/llhttp). EXCLUDE_FROM_ALL
# until a consumer links it.
set(_http "${CMAKE_SOURCE_DIR}/component/corehttp_config/corehttp/source")
set(_llhttp "${_http}/dependency/3rdparty/llhttp")

add_library(corehttp_config STATIC EXCLUDE_FROM_ALL
    "${_http}/core_http_client.c"
    "${_llhttp}/src/api.c"
    "${_llhttp}/src/http.c"
    "${_llhttp}/src/llhttp.c"
)

# PUBLIC: core_http_client.h pulls core_http_config_defaults.h -> core_http_config.h, so
# every consumer of <core_http_client.h> needs this dir on its path too. SYSTEM to keep
# upstream's headers (and llhttp's) out of -Werror.
target_include_directories(corehttp_config SYSTEM PUBLIC
    "${_http}/include"               # -> <core_http_client.h>, *_defaults.h
    "${_llhttp}/include"             # -> <llhttp.h> (the bundled parser)
    "${_http}/interface"             # -> <transport_interface.h>
    "${CMAKE_CURRENT_SOURCE_DIR}"    # -> "core_http_config.h"
)

# Vendor source (core_http_client.c + the bundled llhttp parser): silence warnings under
# the SDK's strict -Wall/-Wextra/-Werror stack — same treatment as zlib / cjson / littlefs.
target_compile_options(corehttp_config PRIVATE -w)
