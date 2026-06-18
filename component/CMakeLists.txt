# component/ — the component-subtree parent. Entered from the root CMakeLists.txt for
# whichever platform is active (BOUFFALO_CHIP or AMEBAD_CHIP). Each platform owns its
# own branch below, so the tree stays STRICT: a directory only add_subdirectory()s things
# beneath it — never a sibling reached via an absolute "${CMAKE_SOURCE_DIR}/component/..."
# path + explicit binary dir. The consumer-coupled cross-platform *_config components that
# the AmebaD KM4 image links (freertos-plus-tcp / wolfssl / ngtcp2 / coremqtt / corehttp /
# freertos-cli / nimble / syscall) used to be reached that way from component/amebad; they
# now live in the AmebaD branch here as proper children of component/.
#
# The universal *_config components that EVERY build needs (zlib, cjson, littlefs, console,
# tlsf_config, freertos-kernel, freertos-posix) are added one level up in the root
# CMakeLists.txt, before src/. By the time we get here `FreeRTOS` / `freertos_config` /
# `at_console` / `littlefs` already exist, so the AmebaD config dirs below can link them.

# =====================================================================
# Bouffalo Lab BL602 / BL616 / BL616CL
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
#   lfs_image            host-side helper to pack a directory tree into
#                        a LittleFS image (component/lfs_image/CMakeLists.txt
#                        is dual-mode: registers lfs_image_add() here,
#                        becomes a standalone host build when invoked
#                        via ExternalProject_Add from inside that helper).
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

    # -----------------------------------------------------------------
    # lfs_image — host-side LittleFS image builder. Dual-mode CMakeLists:
    # add_subdirectory here registers the helper function lfs_image_add();
    # the standalone host sub-build is launched separately via
    # ExternalProject_Add when a consumer calls lfs_image_add().
    # -----------------------------------------------------------------
    add_subdirectory(lfs_image)
endif()

# =====================================================================
# Realtek Ameba D (RTL8721D) — dual-core (KM0 LP + KM4 HP)
# =====================================================================
#
# Entered when a chip toolchain file (cmake/amebad-km{0,4}.cmake) has set AMEBAD_CHIP.
# We add the consumer-coupled cross-platform config components the KM4 image links FIRST
# (as direct children of component/), THEN the in-tree AmebaD subtree (component/amebad)
# whose ports/app link those targets. Defining the config targets before add_subdirectory(amebad)
# is what keeps the in-tree consumers' target_link_libraries(... <config>) resolvable.
if(AMEBAD_CHIP MATCHES "^amebad-km[04]$")
    # QUIC self-test option (ngtcp2 + picotls TLS 1.3 on the mbedTLS/PSA backend — wolfSSL
    # was retired). Read by component/amebad/app_km4. ngtcp2's crypto is built only when ON.
    option(AMEBAD_ENABLE_QUIC "Build the ngtcp2 + picotls/mbedTLS QUIC client self-test" OFF)

    # Cross-platform config components the AmebaD KM4 image links. All EXCLUDE_FROM_ALL
    # until a consumer (the in-tree amebad ports/app, added below) links them. Ordering:
    #   freertos-plus-tcp_config  before  syscall_config   (syscall PRIVATE-links it)
    #   mbedtls_config            before  ngtcp2_config/picotls_config (they link mbedcrypto)
    add_subdirectory(freertos-plus-tcp_config)  # FreeRTOS-Plus-TCP V4.0.0 core stack + FreeRTOSIPConfig.h (freertos_plus_tcp / freertos-plus-tcp_config)
    add_subdirectory(ngtcp2_config)             # ngtcp2 QUIC + the picotls crypto binding (built only when AMEBAD_ENABLE_QUIC)
    add_subdirectory(coremqtt_config)           # coreMQTT v5.0.2 static lib + core_mqtt_config.h
    add_subdirectory(backoffalgorithm_config)   # backoffAlgorithm v1.4.2 (reconnect backoff-with-jitter) — coreMQTT companion
    add_subdirectory(corehttp_config)           # coreHTTP v3.1.3 static lib + core_http_config.h
    add_subdirectory(coresntp_config)           # coreSNTP SNTPv4 client static lib + core_sntp_config.h (built only when AMEBAD_ENABLE_SNTP)
    add_subdirectory(freertos-cli_config)       # FreeRTOS-Plus-CLI V202212.00 static lib (config via FreeRTOSConfig.h)
    add_subdirectory(nimble_config)             # INTERFACE: NimBLE syscfg + upstream include surface
    add_subdirectory(syscall_config)            # newlib retarget OBJECT lib (VFS + malloc + byte-order); PRIVATE-links freertos-plus-tcp_config + at_console
    add_subdirectory(rxi_log_config)            # rxi log.c (leveled logger, default fprintf->stderr) + FreeRTOS-mutex lock via log_set_lock (log_lock_init)
    # Upstream Mbed TLS v3.6.4 software crypto (the AmebaD config/threading/entropy port lives
    # in component/amebad/mbedtls_port, added with the amebad subtree below; it attaches the config
    # onto these targets at generate). Modern backend for corePKCS11 (and the planned
    # picotls/ngtcp2 QUIC stack). Coexists with the ROM mbedTLS 2.4.0 path.
    add_subdirectory(mbedtls_config)            # -> mbedcrypto / mbedx509 / mbedtls (EXCLUDE_FROM_ALL)
    add_subdirectory(corepkcs11_config)         # PKCS#11 software token over mbedcrypto/mbedx509 (PAL is consumer-supplied)
    add_subdirectory(picotls_config)            # TLS 1.3 core + mbedTLS/PSA backend (picotls_mbedtls) — wolfSSL replacement for ngtcp2 QUIC
    add_subdirectory(network_transport)         # TransportInterface_t impls (network_transport_mbedtls/_bio/_sockets) over mbedtls_config + freertos-plus-tcp_config — coreMQTT TLS transport

    # The AmebaD subtree itself (fwlib + per-core HAL + FreeRTOS port + wifi/BT + bootloaders
    # + image2 apps). Its in-tree ports (freertos_tcp_port / nimble_port / syscall_port / ...)
    # and the app_km{0,4} images consume the config targets added just above.
    add_subdirectory(amebad)
endif()
