# component/bouffalo_sdk/nimble_port — Apache NimBLE 1.9.0 host stack port for
# Bouffalo BL602 / BL616 / BL616CL.
#
# The Bouffalo-specific half of the NimBLE integration (mirrors component/amebad/
# nimble_port): it compiles the upstream NimBLE host + porting + transport + mesh +
# tinycrypt sources together with our platform transport (transport_bflb.c) and the
# SHARED app + SM-crypto glue (component/nimble_config/{nimble_app.c,mbedtls_sm_glue.c},
# compiled by path) into the `nimble` static lib, sitting on top of the on-chip
# controller (blecontroller / btblecontroller). The shared nimble_app.c's __weak
# platform hooks keep their defaults here (no Bouffalo override file needed).
#
# Unlike AmebaD (external controller over an HCI UART with an H4 byte stream), the
# Bouffalo controller is on-chip with a STRUCTURED HCI API (bt_onchiphci_send +
# bt_hci_recv_cb), so transport_bflb.c needs no H4 state machine and no RX task.
#
# Source set mirrors amebad's non-Mynewt recipe (file(GLOB) over the vendored
# upstream dirs). Config/headers come from the shared component/nimble_config
# (its syscfg.h + the upstream include surface — replicated below, since we do NOT
# link the nimble_config lib: that lib compiles amebad's nimble_app.c which won't
# build here). SM crypto rides upstream mbedTLS. Built only when AT_ENABLE_NIMBLE.

set(_nb "${AT_ROOT}/component/nimble_config/nimble")
if(NOT EXISTS "${_nb}/nimble/host/src/ble_hs.c")
    message(FATAL_ERROR
        "component/bouffalo_sdk/nimble_port: NimBLE submodule missing — run "
        "`git submodule update --init component/nimble_config/nimble`")
endif()

# Chip -> controller target (INTERFACE lib carrying the prebuilt .a + hci_onchip.h).
if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    set(_ctrl blecontroller)
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616" OR "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    set(_ctrl btblecontroller)
else()
    message(FATAL_ERROR "nimble_port: unsupported BOUFFALO_CHIP '${BOUFFALO_CHIP}'")
endif()

file(GLOB _nb_src
    "${_nb}/porting/nimble/src/*.c"
    "${_nb}/porting/npl/freertos/src/*.c"
    "${_nb}/nimble/src/*.c"
    "${_nb}/nimble/host/src/*.c"
    "${_nb}/nimble/host/util/src/*.c"
    "${_nb}/nimble/host/services/ans/src/*.c"
    "${_nb}/nimble/host/services/bas/src/*.c"
    "${_nb}/nimble/host/services/gap/src/*.c"
    "${_nb}/nimble/host/services/gatt/src/*.c"
    "${_nb}/nimble/host/services/dis/src/*.c"
    "${_nb}/nimble/host/services/ias/src/*.c"
    "${_nb}/nimble/host/services/lls/src/*.c"
    "${_nb}/nimble/host/services/tps/src/*.c"
    "${_nb}/nimble/host/store/ram/src/*.c"            # in-RAM bond store (bflb bring-up)
    "${_nb}/nimble/transport/src/*.c"
    "${_nb}/nimble/transport/common/hci_h4/src/*.c"   # H4 helpers (linked; bflb transport is structured, unused)
    "${_nb}/nimble/host/mesh/src/*.c"                 # BLE Mesh
    "${_nb}/ext/tinycrypt/src/*.c"                    # SM CMAC + helpers (AES/ECC swapped to mbedTLS below)
)

# SM crypto on mbedTLS, not tinycrypt: drop the tinycrypt AES block + micro-ecc
# P-256; tc_aes_encrypt / uECC_* come from component/nimble_config/mbedtls_sm_glue.c.
# cmac_mode.c / utils.c / aes_decrypt.c stay (CMAC rides on our tc_aes_encrypt).
list(REMOVE_ITEM _nb_src
    "${_nb}/ext/tinycrypt/src/aes_encrypt.c"
    "${_nb}/ext/tinycrypt/src/ecc.c"
    "${_nb}/ext/tinycrypt/src/ecc_dh.c"
    "${_nb}/ext/tinycrypt/src/ecc_dsa.c"
    "${_nb}/ext/tinycrypt/src/ecc_platform_specific.c"
)

# Drop mesh CLI/shell/example-model helpers (need <console/console.h> + the Mynewt shell).
list(REMOVE_ITEM _nb_src
    "${_nb}/nimble/host/mesh/src/shell.c"
    "${_nb}/nimble/host/mesh/src/testing.c"
    "${_nb}/nimble/host/mesh/src/light_model.c"
    "${_nb}/nimble/host/mesh/src/model_cli.c"
    "${_nb}/nimble/host/mesh/src/model_srv.c"
)

# Nordic/controller-side timer backend — not needed for a host-only build.
list(REMOVE_ITEM _nb_src
    "${_nb}/porting/nimble/src/hal_timer.c"
    "${_nb}/porting/nimble/src/os_cputime.c"
    "${_nb}/porting/nimble/src/os_cputime_pwr2.c"
)

add_library(nimble STATIC EXCLUDE_FROM_ALL
    ${_nb_src}
    "${CMAKE_CURRENT_SOURCE_DIR}/transport_bflb.c"              # structured on-chip HCI transport (LL side)
    "${AT_ROOT}/component/nimble_config/nimble_app.c"           # SHARED nimble_start() — GAP/GATT sample + host task
    "${AT_ROOT}/component/nimble_config/mbedtls_sm_glue.c"      # SM AES/AES-CMAC/ECDH P-256 over mbedTLS
    "${AT_ROOT}/component/nimble_config/ble_throughput.c"       # SHARED BLE throughput test (central/peripheral) + `bletp`
)

# ble_throughput.c's `bletp` command needs <FreeRTOS_CLI.h> (include-only; FreeRTOS_CLI.c is
# already in the image via the console). Same genex-include pattern as the mbedTLS headers.
target_include_directories(nimble SYSTEM PRIVATE
    "$<TARGET_PROPERTY:freertos_cli,INTERFACE_INCLUDE_DIRECTORIES>")

# Config + header surface (our syscfg.h FIRST, then the upstream NimBLE includes),
# replicated from component/nimble_config's CMakeLists (we don't link that lib).
# SYSTEM so the vendored headers don't trip -Wall -Wextra -Werror. Plus the
# controller headers (hci_onchip.h / *_lib_api.h) via the linked controller target.
target_include_directories(nimble SYSTEM PUBLIC
    "${AT_ROOT}/component/nimble_config/include"      # our syscfg/syscfg.h (must be FIRST)
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
    "${_nb}/nimble/transport/common/hci_h4/include"
    "${_nb}/nimble/host/mesh/include"
    "${_nb}/ext/tinycrypt/include"
)

# FreeRTOS (npl_os_freertos.c + nimble_app.c) + the on-chip controller
# (bt_onchiphci_*/ble_controller_init + hci_onchip.h) + bflb_lhal (SoC headers) +
# mbedTLS for mbedtls_sm_glue.c: mbedcrypto (mbedtls_ecp/aes + its config surface,
# incl. the mbedtls_cfg user config) and mbedtls_config (the FreeRTOS
# threading-alt impl: mbedtls_threading_freertos_init). Real link edges so CMake
# orders `nimble` before the mbedtls archives at final link.
target_link_libraries(nimble PUBLIC
    freertos_kernel
    freertos_config
    freertos_port
    bflb_lhal
    ${_ctrl}
    mbedcrypto
    mbedtls_config
)

# The default BLE advert name comes from the shared nimble_app.c weak hook = the network hostname
# (bl602_<mac> / HOSTNAME); NIMBLE_DEV_NAME stays the built-in "at-nimble" empty-hostname fallback.

# Vendored upstream — silence its warnings (-w last overrides the global -Werror).
target_compile_options(nimble PRIVATE -w)
