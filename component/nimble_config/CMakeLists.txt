# component/nimble_config — Apache NimBLE 1.9.0 config + portable app/crypto layer.
#
# The platform-independent half of the NimBLE integration:
#   - the config + header surface: our syscfg.h overrides (include/syscfg/syscfg.h)
#     plus every upstream NimBLE public include dir;
#   - the portable application + SM-crypto glue, compiled into the `nimble_config`
#     lib: nimble_app.c (amebad_nimble_start() — GAP/GATT sample peripheral + host
#     task) and mbedtls_sm_glue.c (SM AES/AES-CMAC/ECDH P-256 over mbedTLS,
#     replacing NimBLE's bundled tinycrypt AES + micro-ecc). Both use plain printf
#     (newlib -> console), no platform logger.
#
# The platform-specific transport + coex live in the port (component/amebad/
# nimble_port), which compiles the upstream NimBLE sources and links this. The two
# call into each other (this -> port: coex/transport bring-up; port -> this:
# tc_aes_encrypt/uECC_* for the upstream tinycrypt CMAC), so the final image lists
# both inside one --start-group (app_km4's cross_refs LINK_GROUP).
#
# Upstream is the submodule component/nimble_config/nimble (pinned nimble_1_9_0_tag).
# Cross-platform: no chip gate — built wherever nimble_config is linked
# (EXCLUDE_FROM_ALL-style: only the consuming port pulls it). It needs FreeRTOS +
# the mbedcrypto build on whatever platform builds it.

set(_nb "${AT_ROOT}/component/nimble_config/nimble")

add_library(nimble_config STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/nimble_app.c"         # amebad_nimble_start() + host task (printf)
    "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls_sm_glue.c"    # SM AES/ECDH on upstream mbedTLS (replaces tinycrypt + the old wolfCrypt glue)
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_store_lfs.c"      # LittleFS-persistent BLE store (bonds survive reboot)
)

# Config + header surface. PUBLIC so both this lib's own sources and the port see
# it. Include ORDER matters — our include/ is FIRST so syscfg/syscfg.h shadows the
# upstream porting one and its #include_next finds the upstream defaults next.
# SYSTEM: the vendored headers must not trip the -Wall -Wextra -Werror stack.
target_include_directories(nimble_config SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"             # our syscfg/syscfg.h (must be FIRST)
    "${_nb}/porting/nimble/include"
    "${_nb}/porting/npl/freertos/include"
    "${_nb}/nimble/include"
    "${_nb}/nimble/host/include"
    "${_nb}/nimble/host/util/include"
    "${_nb}/nimble/host/services/ans/include"
    "${_nb}/nimble/host/services/bas/include"
    "${_nb}/nimble/host/services/bleuart/include"
    "${_nb}/nimble/host/services/gap/include"
    "${_nb}/nimble/host/services/gatt/include"
    "${_nb}/nimble/host/services/dis/include"
    "${_nb}/nimble/host/services/ias/include"
    "${_nb}/nimble/host/services/lls/include"
    "${_nb}/nimble/host/services/tps/include"
    "${_nb}/nimble/host/store/ram/include"
    "${_nb}/nimble/transport/include"
    "${_nb}/nimble/transport/common/hci_h4/include"   # nimble/transport/hci_h4.h
    "${_nb}/nimble/host/mesh/include"
    "${_nb}/ext/tinycrypt/include"
)

# FreeRTOS for nimble_app.c (nimble_port_freertos.h: task create + host task).
target_link_libraries(nimble_config PUBLIC FreeRTOS)

# mbedTLS headers + the AmebaD MBEDTLS_USER_CONFIG_FILE for mbedtls_sm_glue.c — COMPILE
# requirements only, NOT a link edge (the mbedtls_* symbols resolve via app_km4's
# cross_refs LINK_GROUP, which lists mbedcrypto + mbedtls_port). mbedcrypto carries
# the amebad config (via amebad_mbedtls_cfg); the gen-exprs resolve at generate regardless
# of subdir order. (wolfCrypt migration — see component/amebad/mbedtls_port.)
target_include_directories(nimble_config SYSTEM PRIVATE
    $<TARGET_PROPERTY:mbedcrypto,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(nimble_config PRIVATE
    $<TARGET_PROPERTY:mbedcrypto,INTERFACE_COMPILE_DEFINITIONS>)

# Our glue rides on the vendored tinycrypt/NimBLE headers — silence their warnings
# (the AmebaD flag stack is -Wall -Wextra -Werror). -w (last) overrides -Werror.
target_compile_options(nimble_config PRIVATE -w)
