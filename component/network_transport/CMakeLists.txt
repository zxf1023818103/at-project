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
