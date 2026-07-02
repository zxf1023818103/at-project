# network_transport — the TransportInterface_t implementations coreMQTT / coreHTTP need.
#
# VENDORED (not a submodule): these files live inside the FreeRTOS/FreeRTOS monorepo
# (FreeRTOS-Plus/Source/Application-Protocols/network_transport) with no standalone repo, so
# they are copied in rather than added as a giant submodule. Source commit pinned at:
#   FreeRTOS/FreeRTOS @ b72aef2d316c0571eaf6a9e528e759b8e2ba107b  (network_transport, MIT)
# Re-sync by re-copying from that path at a newer SHA and bumping this comment. The upstream
# subdirectory layout (tcp_sockets_wrapper/include, .../ports/freertos_plus_tcp, logging/)
# is FLATTENED here — every file sits in this dir.
#
# What was taken (and what was left out):
#   kept  — tcp_sockets_wrapper.{c,h} (freertos_plus_tcp port, the TCP_Sockets_* API),
#           transport_plaintext, transport_mbedtls (+ the mbedTLS BIO glue),
#           transport_mbedtls_pkcs11 (+ mbedtls_pk_pkcs11 / mbedtls_rng_pkcs11).
#   left  — transport_wolfSSL.* (the project is migrating wolfSSL -> mbedTLS + picotls),
#           the cellular sockets port (we use FreeRTOS-Plus-TCP), and sockets_wrapper.* (a
#           separate legacy Sockets_* API none of the kept transports call — they use
#           TCP_Sockets_*).
#   added — logging_{levels,stack}.h: the FreeRTOS Log* facade the transport sources #include
#           directly. The light printf-backed variant (vendored from corePKCS11), NOT the
#           heavy FreeRTOS-Plus logging-task one (which pulls logging.h + vLoggingPrintf).
#           SdkLog defaults to printf; override LIBRARY_LOG_LEVEL / SdkLog from the consumer
#           to retarget or silence.
#
# STAGED, like corepkcs11_config: no platform branch add_subdirectory's this yet. The mbedTLS
# transports need a configured mbedTLS (component/mbedtls_config) and the PKCS#11 one a
# corePKCS11 token + PAL (component/corepkcs11_config). Wire the variant you want from the
# consuming app. Dependencies are pulled through the existing umbrella targets:
#   coremqtt_config      -> transport_interface.h (the TransportInterface_t)
#   freertos-plus-tcp_config -> FreeRTOS_Sockets.h / FreeRTOS_IP.h / FreeRTOS_DNS.h
#   mbedtls_config       -> mbedcrypto/mbedx509/mbedtls headers+symbols, threading_alt.h
#   corepkcs11_config    -> core_pkcs11.h / pkcs11.h / core_pki_utils.h
#
# Targets produced (pick ONE transport; they are alternatives, all over the shared sockets lib):
#   network_transport_sockets         STATIC — TCP_Sockets_* over FreeRTOS-Plus-TCP.
#   network_transport_plaintext       STATIC — TCP, no TLS.
#   network_transport_mbedtls         STATIC — TLS via mbedTLS (PEM/DER creds).
#   network_transport_mbedtls_pkcs11  STATIC — TLS via mbedTLS, client key in a PKCS#11 token.

# The logging_stack.h macros put __FILE__ in every [PicotlsTransport]/[SocketsWrapper] log line,
# which was the full build path (/home/.../component/network_transport/transport_picotls.c). Map this
# dir prefix away so __FILE__ collapses to just the basename (transport_picotls.c). Applies to every
# target defined below in this file (the .c sources all live in this dir).
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

# --- sockets layer: TCP_Sockets_* over FreeRTOS-Plus-TCP -----------------------------------
# The base every transport sits on. tcp_sockets_wrapper.c calls FreeRTOS_socket / _connect /
# _recv / _send / DNS directly (self-contained — it does NOT use the legacy sockets_wrapper).
add_library(network_transport_sockets STATIC EXCLUDE_FROM_ALL
    tcp_sockets_wrapper.c
)
target_include_directories(network_transport_sockets SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(network_transport_sockets PUBLIC freertos-plus-tcp_config freertos_config)
target_compile_options(network_transport_sockets PRIVATE -w)

# --- plaintext transport (no TLS) ----------------------------------------------------------
add_library(network_transport_plaintext STATIC EXCLUDE_FROM_ALL
    transport_plaintext.c
)
target_include_directories(network_transport_plaintext SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(network_transport_plaintext PUBLIC network_transport_sockets coremqtt_config)
target_compile_options(network_transport_plaintext PRIVATE -w)

# --- mbedTLS BIO glue: bridges mbedtls_ssl_set_bio() to TCP_Sockets_* ----------------------
# Shared by both TLS transports below (a static-lib member, so linking it from either is fine).
add_library(network_transport_mbedtls_bio STATIC EXCLUDE_FROM_ALL
    mbedtls_bio_tcp_sockets_wrapper.c
)
target_include_directories(network_transport_mbedtls_bio SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(network_transport_mbedtls_bio PUBLIC network_transport_sockets mbedtls_config)
target_compile_options(network_transport_mbedtls_bio PRIVATE -w)

# --- TLS transport via mbedTLS (in-memory PEM/DER credentials) ------------------------------
add_library(network_transport_mbedtls STATIC EXCLUDE_FROM_ALL
    transport_mbedtls.c
)
target_include_directories(network_transport_mbedtls SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(network_transport_mbedtls PUBLIC
    network_transport_mbedtls_bio network_transport_sockets mbedtls_config coremqtt_config)
target_compile_options(network_transport_mbedtls PRIVATE -w)

# --- TLS transport via mbedTLS with the client key in a PKCS#11 token -----------------------
# mbedtls_pk_pkcs11.c is an mbedTLS PK wrapper that delegates signing to C_Sign — it reaches
# into mbedTLS internals (pk_wrap.h, MBEDTLS_ALLOW_PRIVATE_ACCESS), so the upstream mbedTLS
# library/ dir is on the PRIVATE include path. Needs a configured corePKCS11 (token + PAL).
add_library(network_transport_mbedtls_pkcs11 STATIC EXCLUDE_FROM_ALL
    transport_mbedtls_pkcs11.c
    mbedtls_pk_pkcs11.c
    mbedtls_rng_pkcs11.c
)
target_include_directories(network_transport_mbedtls_pkcs11 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(network_transport_mbedtls_pkcs11 SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../mbedtls_config/mbedtls/library")   # pk_wrap.h (mbedTLS-private)
target_link_libraries(network_transport_mbedtls_pkcs11 PUBLIC
    network_transport_mbedtls_bio network_transport_sockets mbedtls_config corepkcs11_config coremqtt_config)
target_compile_options(network_transport_mbedtls_pkcs11 PRIVATE -w)

# --- TLS transport via picotls (TLS 1.3 on the mbedTLS/PSA crypto backend) -------------------
# Alternative to network_transport_mbedtls; exports the SAME TLS_FreeRTOS_* API (transport_tls.h
# selects the backend). picotls is buffer-based, so it sits directly on network_transport_sockets
# (no mbedTLS BIO). mbedtls_config is linked for the X.509 server-cert verifier + the software
# client-key signer in transport_picotls.c; picotls_config brings picotls_core + picotls_mbedtls.
add_library(network_transport_picotls STATIC EXCLUDE_FROM_ALL
    transport_picotls.c
)
target_include_directories(network_transport_picotls SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(network_transport_picotls PUBLIC
    picotls_config network_transport_sockets mbedtls_config coremqtt_config)
target_compile_options(network_transport_picotls PRIVATE -w)
# Make transport_tls.h resolve to transport_picotls.h for any TU that links this target.
target_compile_definitions(network_transport_picotls PUBLIC TLS_BACKEND_PICOTLS)

# Optional picotls handshake heap tracing (Makefile TLS_HEAP_TRACE=ON). Logs free heap at the
# connect / handshake / disconnect milestones via the gated TLS_HEAP_MARK in transport_picotls.c.
if(AMEBAD_TLS_HEAP_TRACE STREQUAL "ON")
    target_compile_definitions(network_transport_picotls PRIVATE AMEBAD_TLS_HEAP_TRACE)
endif()

# --- compressed CA root bundle (CRT_BUNDLE=ON) ----------------------------------------------
# When enabled (threaded from the Makefile as AMEBAD_ENABLE_CRT_BUNDLE), both TLS transports
# verify the server cert against the embedded Mozilla root bundle (esp_crt_bundle) instead of the
# single pinned root in NetworkCredentials_t.pRootCa. The transport sources gate this on the
# AMEBAD_ENABLE_CRT_BUNDLE macro and call into esp_crt_bundle.h (linked PUBLIC for the headers;
# the actual archive rides app_km4's _aws_grp link group). PUBLIC def so the consuming app TUs
# (aws_iot_demo.c / ota_*.c) that set creds.useCrtBundle see the same macro.
if(AMEBAD_ENABLE_CRT_BUNDLE STREQUAL "ON")
    foreach(_tls network_transport_mbedtls network_transport_picotls)
        target_link_libraries(${_tls} PUBLIC esp_crt_bundle_config)
        target_compile_definitions(${_tls} PUBLIC AMEBAD_ENABLE_CRT_BUNDLE)
    endforeach()
endif()

# --- TLS-1.2 fallback dispatcher (TLS_BACKEND=fallback) --------------------------------------
# Links BOTH backends side by side and tries picotls (TLS 1.3) first, falling back to mbedTLS
# (TLS 1.2/1.3) when the server has no TLS 1.3. Both backends are compiled with their public
# TLS_FreeRTOS_* symbols renamed (picotls_* / mbedtls_*) so they don't collide; transport_fallback.c
# calls the renamed sets. The app links network_transport_fallback (which exports the public
# TLS_FreeRTOS_* + TLS_BACKEND_FALLBACK for transport_tls.h).
if(AMEBAD_TLS_BACKEND STREQUAL "fallback")
    target_compile_definitions(network_transport_picotls PRIVATE TLS_TRANSPORT_RENAME_PICOTLS)
    target_compile_definitions(network_transport_mbedtls PRIVATE TLS_TRANSPORT_RENAME_MBEDTLS)

    # The mbedTLS SSL stack is only the TLS-1.2 fallback path here (1.3 peers ride picotls). Strip
    # TLS 1.3 from it (mbedtls_user_config.h gates on MBEDTLS_TLS12_ONLY) so we can also
    # drop KEEP_PEER_CERTIFICATE (free the peer cert post-handshake). PUBLIC + on all three upstream
    # archives so every TU sees one consistent mbedTLS config (shared struct layouts). picotls's
    # TLS 1.3 is unaffected — it's its own stack over mbedcrypto, not the mbedTLS SSL layer.
    foreach(_mt mbedcrypto mbedx509 mbedtls)
        if(TARGET ${_mt})
            target_compile_definitions(${_mt} PUBLIC MBEDTLS_TLS12_ONLY)
        endif()
    endforeach()

    add_library(network_transport_fallback STATIC EXCLUDE_FROM_ALL
        transport_fallback.c
    )
    target_include_directories(network_transport_fallback SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(network_transport_fallback PUBLIC
        network_transport_picotls network_transport_mbedtls network_transport_mbedtls_bio
        network_transport_sockets mbedtls_config coremqtt_config)
    target_compile_definitions(network_transport_fallback PUBLIC TLS_BACKEND_FALLBACK)
    target_compile_options(network_transport_fallback PRIVATE -w)
endif()
