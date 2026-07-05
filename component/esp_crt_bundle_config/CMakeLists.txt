# component/esp_crt_bundle_config — ESP-IDF's compressed x509 CA root bundle, ported to AmebaD.
#
# Embeds the full Mozilla NSS root store (cacrt_all.pem, ~121 roots) in a COMPRESSED form: each
# root is reduced to its DER subject name + DER public key in a flat, subject-sorted, offset-
# indexed blob (binary-searched by issuer name). ~56 KB of .rodata vs ~190 KB of PEM. The
# verify path is the classic (non-PSA) Mbed TLS one — see esp_crt_bundle.c.
#
# Build-time generation (the AmebaD build has no objcopy / `ld -r -b binary` / INCBIN path):
#   gen_crt_bundle.py -i cacrt_all.pem  -> x509_crt_bundle (raw blob, in the build dir)
#   bin2c.py x509_crt_bundle            -> x509_crt_bundle.c (esp_crt_bundle_start[] + _len)
# Both the .pem and the scripts are committed, so the build is reproducible (needs host python3
# with the `cryptography` package). The generated .c lands in the build tree, not in git.
#
# Modeled on component/c-ares_config: a STATIC `esp_crt_bundle` target + an `esp_crt_bundle_config`
# INTERFACE umbrella. Links mbedtls_config (mbedx509/mbedcrypto for pk/md/x509). EXCLUDE_FROM_ALL
# like the other *_config: compiled only when a TLS backend links it under CRT_BUNDLE=ON.

set(_gen "${CMAKE_CURRENT_BINARY_DIR}")
set(_pem "${CMAKE_CURRENT_LIST_DIR}/cacrt_all.pem")
set(_genpy "${CMAKE_CURRENT_LIST_DIR}/gen_crt_bundle.py")
set(_bin2c "${CMAKE_CURRENT_LIST_DIR}/bin2c.py")

add_custom_command(
    OUTPUT  "${_gen}/x509_crt_bundle.c"
    # gen_crt_bundle.py writes "x509_crt_bundle" into its CWD (the build dir); bin2c wraps it to C.
    COMMAND python3 "${_genpy}" -q -i "${_pem}"
    COMMAND python3 "${_bin2c}" "${_gen}/x509_crt_bundle" "${_gen}/x509_crt_bundle.c"
    WORKING_DIRECTORY "${_gen}"
    DEPENDS "${_pem}" "${_genpy}" "${_bin2c}"
    COMMENT "esp_crt_bundle: generate compressed Mozilla CA root bundle"
    VERBATIM
)

add_library(esp_crt_bundle STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/esp_crt_bundle.c"
    "${_gen}/x509_crt_bundle.c"
)
target_include_directories(esp_crt_bundle PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(esp_crt_bundle PUBLIC mbedtls_config)
target_compile_options(esp_crt_bundle PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `esp_crt_bundle_config`.
add_library(esp_crt_bundle_config INTERFACE)
target_link_libraries(esp_crt_bundle_config INTERFACE esp_crt_bundle)

# `crt_bundle` console command (crt_bundle_cli.c): info / verify <pem> / addca — validate the
# bundle against a cert file or add a CA at runtime. The optional `connect <host> <port>` subcommand
# does a LIVE TLS handshake to check a real server against the bundle; it needs a TLS client
# transport (component/network_transport), so it's compiled in only where that target exists
# (amebad-family). On bl602 the command keeps the portable info/verify/addca ops. Shared across
# platforms: AmebaD registers it in the AWS image; bl602 in main() (crt_bundle_cli_register).
add_library(crt_bundle_cli STATIC EXCLUDE_FROM_ALL crt_bundle_cli.c)
target_link_libraries(crt_bundle_cli PUBLIC esp_crt_bundle freertos_cli rxi_log FreeRTOS)
target_compile_options(crt_bundle_cli PRIVATE -w)
if(TARGET network_transport)
    # transport_tls.h (TLS_FreeRTOS_Connect/Disconnect + NetworkCredentials_t). The symbols resolve
    # at the image link where network_transport is already pulled (amebad AWS group).
    target_compile_definitions(crt_bundle_cli PRIVATE CRT_BUNDLE_HAVE_TLS_TRANSPORT)
    target_link_libraries(crt_bundle_cli PRIVATE network_transport)
endif()
