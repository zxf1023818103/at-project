# component/amebad/nimble_port — Apache NimBLE 1.9.0 host stack port for AmebaD KM4.
#
# The AmebaD-specific half of the NimBLE integration: it compiles the upstream
# host + porting + transport + mesh + tinycrypt sources together with our 2
# platform glue files (transport_amebad.c, coex_amebad.c) into the `nimble` static
# lib, sitting on top of the controller bring-up + HCI transport in
# component/amebad/hci (no btgap.a). The config/headers AND the portable app +
# SM-crypto layer (nimble_app.c, wolfcrypt_sm_glue.c) come from
# component/nimble_config (linked below). Upstream is component/nimble_config/nimble.
#
# Build: the BLE controller is external silicon over our HCI UART, so
#   BLE_TRANSPORT_HS = native (host on KM4), BLE_TRANSPORT_LL = custom
# (transport_amebad.c provides ble_transport_to_ll_*).
#
# Source set mirrors upstream's non-Mynewt recipe
# (component/nimble_config/nimble/porting/nimble/Makefile.{defs,mesh,tinycrypt} +
# the FreeRTOS NPL) — file(GLOB) over the vendored upstream dirs, the same
# wildcard those Makefiles use; that's upstream code, not ours. Our 2 platform
# glue files are listed explicitly. KM4-only; EXCLUDE_FROM_ALL until app_km4 links it.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_nb "${CMAKE_SOURCE_DIR}/component/nimble_config/nimble")
if(NOT EXISTS "${_nb}/nimble/host/src/ble_hs.c")
    message(FATAL_ERROR
        "component/amebad/nimble_port: NimBLE submodule missing — run "
        "`git submodule update --init component/nimble_config/nimble`")
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
    "${_nb}/nimble/host/store/ram/src/*.c"
    "${_nb}/nimble/transport/src/*.c"
    "${_nb}/nimble/transport/common/hci_h4/src/*.c"   # H4 framing state machine (custom transport uses it)
    "${_nb}/nimble/host/mesh/src/*.c"        # BLE Mesh
    "${_nb}/ext/tinycrypt/src/*.c"           # SM CMAC + helpers (AES/ECC swapped to wolfCrypt below)
)

# SM crypto on wolfCrypt, not tinycrypt: drop the tinycrypt AES block + the
# micro-ecc P-256; tc_aes_encrypt / uECC_* are supplied over wolfCrypt in
# component/nimble_config/wolfcrypt_sm_glue.c. cmac_mode.c / utils.c / aes_decrypt.c
# stay (CMAC rides on our tc_aes_encrypt). See that file for the curve_secp256r1 stubs.
list(REMOVE_ITEM _nb_src
    "${_nb}/ext/tinycrypt/src/aes_encrypt.c"
    "${_nb}/ext/tinycrypt/src/ecc.c"
    "${_nb}/ext/tinycrypt/src/ecc_dh.c"
    "${_nb}/ext/tinycrypt/src/ecc_dsa.c"
    "${_nb}/ext/tinycrypt/src/ecc_platform_specific.c"
)

# Drop mesh CLI / shell / example-model helpers — they #include <console/console.h>
# and the Mynewt shell, which we don't provide (and a mesh node doesn't need; the
# app supplies its own models in nimble_app.c).
list(REMOVE_ITEM _nb_src
    "${_nb}/nimble/host/mesh/src/shell.c"
    "${_nb}/nimble/host/mesh/src/testing.c"
    "${_nb}/nimble/host/mesh/src/light_model.c"
    "${_nb}/nimble/host/mesh/src/model_cli.c"
    "${_nb}/nimble/host/mesh/src/model_srv.c"
)

# Nordic/controller-side timer backend — not needed for a host-only build (the host
# uses ble_npl callouts, not os_cputime). hal_timer.c #includes <nrfx.h>. The
# upstream linux/linux_blemesh examples (also host-only) exclude exactly these 3.
list(REMOVE_ITEM _nb_src
    "${_nb}/porting/nimble/src/hal_timer.c"
    "${_nb}/porting/nimble/src/os_cputime.c"
    "${_nb}/porting/nimble/src/os_cputime_pwr2.c"
)

add_library(nimble STATIC EXCLUDE_FROM_ALL
    ${_nb_src}
    "${CMAKE_CURRENT_SOURCE_DIR}/transport_amebad.c"   # custom HCI transport (LL side) -> amebad_hci
    "${CMAKE_CURRENT_SOURCE_DIR}/coex_amebad.c"        # WiFi/BT coex glue -> lib_wlan (P5)
)

# Config + header surface (our syscfg.h FIRST, then the upstream NimBLE includes)
# from the shared component/nimble_config. The include ORDER it sets up makes
# syscfg/syscfg.h shadow the upstream porting one (#include_next finds defaults).
target_link_libraries(nimble PUBLIC
    nimble_config        # syscfg/syscfg.h + the whole upstream NimBLE include surface
    FreeRTOS             # npl_os_freertos.c: FreeRTOS.h / task / queue / semphr / timers
    amebad_hci           # transport TX -> hci_tp_send; bt_hci_bringup_nostack (P2)
    amebad_sdk_includes  # platform_stdlib_rtl8721d.h / DiagPrintf (our glue)
    amebad_wlan          # rltk_coex_* (coex, P5)
)

# porting/npl/freertos/src/npl_os_freertos.c does `#include NIMBLE_NPL_OS_EXTRA_INCLUDE`
# to get CMSIS SCB (for its in_isr() check). Point it at the AmebaD SoC umbrella
# (pulls core_cm33.h via the device header).
target_compile_definitions(nimble PRIVATE "NIMBLE_NPL_OS_EXTRA_INCLUDE=\"ameba_soc.h\"")

# Vendored upstream — silence its warnings (the AmebaD flag stack is -Wall -Wextra
# -Werror). -w (last on the command line) overrides the global -Werror.
target_compile_options(nimble PRIVATE -w)
