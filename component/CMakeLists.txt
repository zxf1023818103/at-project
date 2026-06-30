# component/ — the component-subtree parent. Entered from the root CMakeLists.txt for whichever
# platform is active (BOUFFALO_CHIP or AMEBAD_CHIP). Two parts:
#
#   1. The cross-platform config components (component/*_config + network_transport) are added
#      UNCONDITIONALLY. They are EXCLUDE_FROM_ALL, so they cost nothing for a platform that never
#      links them; defining the targets up front (before the in-tree consumers below) is what keeps
#      every target_link_libraries(... <config>) resolvable on either platform.
#
#   2. The platform subtrees (component/bouffalo_sdk, component/amebad) are gated on
#      BOUFFALO_CHIP / AMEBAD_CHIP — each owns its own branch + the chip-specific companions.
#
# The universal *_config that EVERY build needs (zlib, cjson, littlefs, console, tlsf_config,
# freertos-kernel, freertos-posix) are added FIRST, below — so add_subdirectory(component) is
# self-sufficient: a standalone consumer (each project/amebad image, project/bouffalo) needs only it.
# Guarded so a caller that already added them directly (project/ameba) doesn't double-add.

# =====================================================================
# Universal config components every build needs — added FIRST so the *_config + platform subtrees
# below resolve FreeRTOS / freertos_config / at_console / littlefs / zlibstatic. Ordering:
# freertos-kernel before freertos-posix. Guard: skip if a non-component consumer already added them.
# =====================================================================
if(NOT TARGET FreeRTOS)
    add_subdirectory(zlib_config)
    add_subdirectory(cjson_config)
    add_subdirectory(littlefs_config)
    add_subdirectory(console)
    add_subdirectory(tlsf_config)
    add_subdirectory(freertos-kernel_config)
    add_subdirectory(freertos-posix_config)
endif()

# QUIC self-test option (ngtcp2 + picotls/mbedTLS TLS 1.3 — wolfSSL was retired). AmebaD app_km4
# reads it; ngtcp2_config builds its crypto only when ON. Declared before the block that consumes it.
option(AMEBAD_ENABLE_QUIC "Build the ngtcp2 + picotls/mbedTLS QUIC client self-test" OFF)

# =====================================================================
# AmebaD-coupled cross-platform config components. Gated to NON-Bouffalo: Bouffalo brings its own
# mbedtls/lwip via bouffalo_sdk (which add_subdirectory()s mbedtls_config itself — adding it here too
# double-defines mbedcrypto), and needs none of the AWS/QUIC/NimBLE configs. All EXCLUDE_FROM_ALL.
# Ordering: freertos-plus-tcp_config before syscall_config; mbedtls_config before ngtcp2/picotls.
# =====================================================================
if(NOT BOUFFALO_CHIP)
add_subdirectory(freertos-plus-tcp_config)  # FreeRTOS-Plus-TCP V4.0.0 core stack + FreeRTOSIPConfig.h
add_subdirectory(ngtcp2_config)             # ngtcp2 QUIC + the picotls crypto binding (built only when AMEBAD_ENABLE_QUIC)
add_subdirectory(coremqtt_config)           # coreMQTT v5.0.2 static lib + core_mqtt_config.h
add_subdirectory(coremqtt-agent_config)     # coreMQTT-Agent v2.0.0 (thread-safe wrapper) — links coremqtt_config
add_subdirectory(backoffalgorithm_config)   # backoffAlgorithm v1.4.2 (reconnect backoff-with-jitter)
add_subdirectory(corehttp_config)           # coreHTTP v3.1.3 static lib + core_http_config.h
add_subdirectory(corejson_config)           # coreJSON parser (OTA job-document parsing) — ota_config dep
add_subdirectory(tinycbor_config)           # TinyCBOR (OTA MQTT data-plane block decode) — ota_config dep
add_subdirectory(ota_config)                # AWS IoT OTA Agent v3.3.0 — links corejson_config + tinycbor_config + freertos_config
add_subdirectory(coresntp_config)           # coreSNTP SNTPv4 client static lib + core_sntp_config.h
add_subdirectory(freertos-cli_config)       # FreeRTOS-Plus-CLI V202212.00 static lib
add_subdirectory(nimble_config)             # INTERFACE: NimBLE syscfg + upstream include surface
add_subdirectory(mcumgr_config)             # apache/mynewt-mcumgr 0.7.0 SMP core (img+os groups) over tinycbor v7 shim — BLE DFU (MCUMGR=ON)
add_subdirectory(syscall_config)            # newlib retarget OBJECT lib (VFS + malloc + byte-order); PRIVATE-links freertos-plus-tcp_config + at_console
add_subdirectory(c-ares_config)             # c-ares async DNS resolver (compiled only when linked / targeted; wired in under CARES=ON)
add_subdirectory(rxi_log_config)            # rxi log.c (leveled logger) + FreeRTOS-mutex lock via log_set_lock
add_subdirectory(mbedtls_config)            # upstream Mbed TLS v3.6.4 -> mbedcrypto / mbedx509 / mbedtls
add_subdirectory(esp_crt_bundle_config)     # compressed Mozilla CA root bundle (CRT_BUNDLE=ON) over mbedtls_config
add_subdirectory(corepkcs11_config)         # PKCS#11 software token over mbedcrypto/mbedx509 (PAL consumer-supplied)
add_subdirectory(picotls_config)            # TLS 1.3 core + mbedTLS/PSA backend (picotls_mbedtls) for ngtcp2 QUIC
add_subdirectory(network_transport)         # TransportInterface_t impls over mbedtls_config + freertos-plus-tcp_config — coreMQTT TLS transport
endif()  # NOT BOUFFALO_CHIP

# lfs_image — host-side LittleFS image builder (cross-platform). Dual-mode CMakeLists: add_subdirectory
# here registers the helper function lfs_image_add(); the standalone host sub-build is launched
# separately via ExternalProject_Add when a consumer (either platform) calls lfs_image_add().
add_subdirectory(lfs_image)

# =====================================================================
# Platform: Bouffalo Lab BL602 / BL616 / BL616CL
# =====================================================================
#
#   bouffalo_sdk         the Bouffalo Lab SDK extraction (HAL + chip-std
#                        + sysinit + port adapters + wifi/BT/RF stacks).
#                        Umbrella INTERFACE target `bouffalo_sdk` is what
#                        consumers link. Also pulls in upstream lwip
#                        (via bflb_lwip_port) and upstream mbedtls (after
#                        the three bflb_mbedtls_* port adapters).
#   lwip_dhcpd           Bouffalo's raw DHCP-server-on-lwip app, lifted
#                        out of the lwip submodule so it can depend on
#                        the upstream-clean lwip + freertos_kernel.
#   lfs_easyflash        EasyFlash KV API on top of upstream littlefs.
# (lfs_image — the cross-platform host-side LittleFS image builder — is added unconditionally above.)
if(BOUFFALO_CHIP)
    # Target chip selection. Typical usage is to set this through a chip
    # toolchain file (cmake/bl602-toolchain.cmake etc., which also pins
    # -march / -mabi / sysroot) but a plain -DBOUFFALO_CHIP=bl602 on the
    # cmake command line works too — the CACHE STRING declaration here
    # makes the value visible in cmake-gui / ccmake and the STRINGS
    # property gives a dropdown of supported chips. set() with CACHE does
    # not overwrite an existing value, so this is a no-op when the
    # toolchain file has already set it.
    set(BOUFFALO_CHIP "${BOUFFALO_CHIP}" CACHE STRING
        "Bouffalo target chip — required (bl602 | bl616 | bl616cl)")
    set_property(CACHE BOUFFALO_CHIP PROPERTY STRINGS
        bl602 bl616 bl616cl)
    if(NOT BOUFFALO_CHIP MATCHES "^(bl602|bl616|bl616cl)$")
        message(FATAL_ERROR
            "BOUFFALO_CHIP='${BOUFFALO_CHIP}' is not a supported chip. "
            "Expected one of: bl602, bl616, bl616cl.")
    endif()

    # ASM is an SDK concern (startup code, FreeRTOS port asm, vendor archive
    # stubs — 700+ .S files in components/bouffalo_sdk + freertos-kernel),
    # not a user-application one. Enable it here so projects can keep their
    # project() line to the languages they actually write (C / CXX).
    #
    # bflb_add_executable() (the ELF -> flashable BIN pipeline) is provided
    # by component/bouffalo_sdk/bflb_image_tool/CMakeLists.txt, pulled in
    # transitively by add_subdirectory(bouffalo_sdk).
    enable_language(ASM)

    # Third-party submodules are added by the root CMakeLists.txt before we
    # get here, so zlibstatic / cjson / littlefs are already real targets that
    # bouffalo_sdk's umbrella can pull into its INTERFACE link list.
    add_subdirectory(bouffalo_sdk)

    # lwip_dhcpd / lfs_easyflash live at this level (not inside bouffalo_sdk) because they
    # depend on targets the bouffalo_sdk subtree defines (lwipcore / bflb_littlefs_port).
    # Each self-registers into the bouffalo_sdk umbrella aggregate from its OWN CMakeLists
    # (target_link_libraries(bouffalo_sdk INTERFACE <self>) there), and the --start/--end
    # link-group is owned by bouffalo_sdk/CMakeLists.txt — so this subtree umbrella stays
    # add_subdirectory-only.
    add_subdirectory(lwip_dhcpd)
    add_subdirectory(lfs_easyflash)
endif()

# =====================================================================
# Platform: Realtek Ameba D (RTL8721D) — dual-core (KM0 LP + KM4 HP)
# =====================================================================
#
# Entered when a chip toolchain file (cmake/amebad-km{0,4}.cmake) has set AMEBAD_CHIP. The AmebaD
# subtree (fwlib + per-core HAL + FreeRTOS port + wifi/BT + bootloaders + image2 apps); its in-tree
# ports (freertos_tcp_port / nimble_port / syscall_port / ...) and the app_km{0,4} images consume
# the cross-platform config targets added unconditionally above.
if(AMEBAD_CHIP MATCHES "^amebad-km[04]$")
    add_subdirectory(amebad)
endif()
