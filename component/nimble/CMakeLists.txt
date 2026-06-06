# component/nimble — Apache NimBLE 1.9.0 host stack glue for AmebaD KM4.
#
# Upstream NimBLE is the submodule component/thirdparty/nimble (pinned
# nimble_1_9_0_tag). This component compiles the upstream host + porting +
# transport + mesh + tinycrypt sources and our 3 glue files, sitting on top of
# the controller bring-up + HCI transport in component/amebad/hci (no btgap.a).
#
# Build: the BLE controller is external silicon over our HCI UART, so
#   BLE_TRANSPORT_HS = native (host on KM4), BLE_TRANSPORT_LL = custom
# (transport_amebad.c provides ble_transport_to_ll_*). Config is our
# include/syscfg/syscfg.h (overrides + #include_next upstream defaults).
#
# Source set mirrors upstream's non-Mynewt recipe
# (component/thirdparty/nimble/porting/nimble/Makefile.{defs,mesh,tinycrypt} +
# the FreeRTOS NPL) — file(GLOB) over the vendored upstream dirs, the same
# wildcard those Makefiles use; that's upstream code, not ours. Our 3 glue files
# are listed explicitly. KM4-only; EXCLUDE_FROM_ALL until app_km4 links it.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_nb "${CMAKE_SOURCE_DIR}/component/thirdparty/nimble")
if(NOT EXISTS "${_nb}/nimble/host/src/ble_hs.c")
    message(FATAL_ERROR
        "component/nimble: NimBLE submodule missing — run "
        "`git submodule update --init component/thirdparty/nimble`")
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
# micro-ecc P-256, and supply tc_aes_encrypt / uECC_* over wolfCrypt in
# wolfcrypt_sm_glue.c. cmac_mode.c / utils.c / aes_decrypt.c stay (CMAC rides on
# our tc_aes_encrypt). See wolfcrypt_sm_glue.c header for the curve_secp256r1 stubs.
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
    "${CMAKE_CURRENT_SOURCE_DIR}/wolfcrypt_sm_glue.c"  # SM AES/ECDH on wolfCrypt (replaces tinycrypt)
    "${CMAKE_CURRENT_SOURCE_DIR}/nimble_app.c"         # amebad_nimble_start() + host task
)

# Include order matters: our include/ is FIRST so include/syscfg/syscfg.h shadows
# the upstream porting one and its #include_next finds the upstream defaults next.
target_include_directories(nimble SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
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

target_link_libraries(nimble PUBLIC
    FreeRTOS              # npl_os_freertos.c: FreeRTOS.h / task / queue / semphr / timers
    amebad_hci           # transport TX -> hci_tp_send; bt_hci_bringup_nostack (P2)
    amebad_sdk_includes  # platform_stdlib.h / DiagPrintf (our glue)
    amebad_wlan          # rltk_coex_* (coex, P5)
)

# wolfCrypt headers + WOLFSSL_USER_SETTINGS for wolfcrypt_sm_glue.c — as COMPILE
# requirements only, NOT a link edge. amebad_wolfssl's wc_* symbols link into the
# executable via app_km4's cross_refs LINK_GROUP (nimble is in that group). A real
# link dep here would cycle: nimble ∈ group, and amebad_wolfssl needs FreeRTOS which
# is also ∈ group. The generator expressions resolve at generate time regardless of
# subdirectory order.
target_include_directories(nimble SYSTEM PRIVATE
    $<TARGET_PROPERTY:amebad_wolfssl,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(nimble PRIVATE
    $<TARGET_PROPERTY:amebad_wolfssl,INTERFACE_COMPILE_DEFINITIONS>)

# porting/npl/freertos/src/npl_os_freertos.c does `#include NIMBLE_NPL_OS_EXTRA_INCLUDE`
# to get CMSIS SCB (for its in_isr() check). Point it at the AmebaD SoC umbrella
# (pulls core_cm33.h via the device header).
target_compile_definitions(nimble PRIVATE "NIMBLE_NPL_OS_EXTRA_INCLUDE=\"ameba_soc.h\"")

# Vendored upstream — silence its warnings (the AmebaD flag stack is -Wall -Wextra
# -Werror). -w (last on the command line) overrides the global -Werror.
target_compile_options(nimble PRIVATE -w)
