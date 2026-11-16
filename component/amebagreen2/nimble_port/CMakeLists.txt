set(_nb "${AT_ROOT}/component/nimble_config/nimble")
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
    "${_nb}/nimble/transport/src/*.c"
    "${_nb}/nimble/transport/common/hci_h4/src/*.c"   # H4 framing state machine (custom transport uses it)
    "${_nb}/nimble/host/mesh/src/*.c"        # BLE Mesh
    "${_nb}/ext/tinycrypt/src/*.c"           # SM CMAC + helpers (AES/ECC swapped to mbedTLS below)
)

# SM crypto on mbedTLS, not tinycrypt: drop the tinycrypt AES block + the
# micro-ecc P-256; tc_aes_encrypt / uECC_* are supplied over mbedTLS in
# component/nimble_config/mbedtls_sm_glue.c. cmac_mode.c / utils.c / aes_decrypt.c
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

add_library(nimble STATIC EXCLUDE_FROM_ALL ${_nb_src} transport.c)
target_link_libraries(nimble PUBLIC nimble_config FreeRTOS PRIVATE amebagreen2_hci amebagreen2_sdk)
target_compile_options(nimble PRIVATE -w)
target_compile_definitions(nimble PRIVATE NDEBUG)
