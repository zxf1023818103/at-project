# bouffalo_sdk — umbrella INTERFACE library that links all chip-relevant
# Bouffalo components in one shot. Consumers do:
#
#   target_link_libraries(<exe> PRIVATE bouffalo_sdk)
#
# and pick up the low-level HAL, board init sources, linker script, and
# the -ufw_header link option transitively.

# BOUFFALO_CHIP gating happens in component/CMakeLists.txt (the file
# that add_subdirectory's this one). We can rely on it being set + valid
# by the time we get here.

# FreeRTOS-Kernel (component/freertos-kernel_config/freertos-kernel, the bouffalo-port
# branch on the zxf1023818103 fork). Configure it BEFORE add_subdirectory
# so the kernel's CMakeLists can read FREERTOS_PORT + freertos_config.
#
# `freertos_config` is the cross-platform STATIC at the repo root
# (component/freertos-kernel_config), which now also derives FREERTOS_PORT (GCC_RISC_V for
# BOUFFALO_CHIP) + FREERTOS_HEAP and add_subdirectory's the kernel itself — so freertos_kernel
# / FreeRTOS already exist here. Bouffalo's RISC-V-specific config (atomic_rtos.h path + mtime
# / trap-handler defs + the kernel-header feed) is attached to freertos_config from the
# bflb_freertos_port COMPONENT; it resolves at generate time, so it still reaches the kernel's
# compile.
if(NOT TARGET freertos_kernel)
    message(FATAL_ERROR
        "freertos_kernel target missing — component/freertos-kernel_config must be added "
        "(repo-root CMakeLists) before component (bouffalo_sdk)")
endif()

# bflb_autoconf must come first: its INTERFACE forces -include autoconf.h
# into every TU that links it (transitively, via bflb_lhal below).
add_subdirectory(bflb_autoconf)
add_subdirectory(bflb_freertos_port)
add_subdirectory(bflb_sdk_version)
add_subdirectory(bflb_sysinit)
# std must be added before lhal — lhal links against ${BOUFFALO_CHIP}_std.
add_subdirectory(${BOUFFALO_CHIP}_std)
add_subdirectory(bflb_lhal)
add_subdirectory(bflb_log)
# tlsf_config moved to the shared top-level component (component/tlsf_config), added by the
# repo-root CMakeLists before this subtree; the `tlsf_config` target already exists.
add_subdirectory(bflb_block_pool)
add_subdirectory(bflb_timestamp)
add_subdirectory(bflb_partition)
add_subdirectory(bflb_mtd)
add_subdirectory(bflb_sys)
add_subdirectory(bflb_async_event)
# bflb_power_mgmt is a BL616+ thing (clock_manager + pm_manager + tickless
# stack built around the LP-fw framework). BL602 predates all of this —
# its bl_lp.h has no chip-specific impl, several SDK symbols
# (iot2lp_para, HBN_*) don't exist on BL602, and bouffalo's BL602 power-
# mgmt path goes through wifi_hosal's own bflb_pm.c. Skip the lib on
# BL602 to avoid pulling in code that won't compile.
if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(bflb_power_mgmt)
endif()
# FreeRTOS-Plus-TCP NetworkInterface for the wl80211 driver (BL616 / BL616CL) —
# replaces the removed lwIP netif glue. Self-gates to the wl80211 chips (early
# return() on bl602); re-supplies wl80211_tcpip_input / wl80211_output_raw.
add_subdirectory(freertos_tcp_port)

# Bouffalo's littlefs XIP-flash backend (lfs_xip_init + the four lfs_config
# read/prog/erase/sync callbacks). Always built so consumers of the
# `littlefs` target from component/thirdparty/ find a backend; tiny
# when nothing links it.
add_subdirectory(bflb_littlefs_port)

# mbedtls port surface — mbedtls_port carries the runtime glue + HACC/PKA-backed alt impls AND
# (via its mbedtls_cfg INTERFACE) the whole Bouffalo mbedTLS config: MBEDTLS_USER_CONFIG_FILE
# + the MBEDTLS_*_ALT selections. Added before the upstream mbedtls pull below; its config
# INTERFACE is attached onto the upstream targets in the foreach further down.
add_subdirectory(mbedtls_port)

# -------------------------------------------------------------------------
# Upstream Mbed TLS v3.6.4 (component/mbedtls_config/mbedtls, clean upstream).
# Pulled here — not in component/thirdparty/CMakeLists.txt — because the
# upstream sources are unbuildable without the mbedtls_port config/headers,
# and nothing outside the Bouffalo SDK consumes upstream mbedtls. Keeps the
# dependency one-directional.
#
# Why v3.6.4 and not v3.6.5+: the 3.6.5 release reorganized the source
# tree (most primitives moved under tf-psa-crypto/drivers/builtin/src/)
# AND removed the classic MBEDTLS_*_ALT mechanism for the in-tree builtin
# drivers. Bouffalo's HW-accelerated alt impls (aes_alt.c, sha*_alt.c,
# bignum_alt.c, ecp_alt.c, …) rely on MBEDTLS_*_ALT to substitute the
# software implementations at compile time. Pinning at 3.6.4 keeps that
# contract intact while still being a supported LTS release.
#
# Targets produced (all STATIC — BUILD_SHARED_LIBS forced OFF):
#   mbedcrypto    crypto primitives + PSA + DRBG
#   mbedx509      X.509 cert handling
#   mbedtls       TLS protocol (depends on the above)
#   everest       Curve25519 fast impl (3rdparty submodule)
#   p256m         secp256r1 fast impl (3rdparty submodule)

# The upstream option knobs (ENABLE_TESTING OFF, USE_STATIC_MBEDTLS_LIBRARY, …) + the
# add_subdirectory of the submodule live in component/mbedtls_config. Bouffalo uses the
# upstream DEFAULT config + MBEDTLS_USER_CONFIG_FILE (mbedtls_user_config.h) — both carried by
# mbedtls_port's mbedtls_cfg INTERFACE, attached onto the upstream targets in the foreach
# below (no -DMBEDTLS_CONFIG_FILE here anymore).
# The port's alt headers (aes_alt.h etc.) need to be on the include path of every TU because
# upstream's public headers #include "aes_alt.h" unconditionally when MBEDTLS_AES_ALT is set.
include_directories("${CMAKE_CURRENT_LIST_DIR}/mbedtls_port")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../mbedtls_config"
                 ${CMAKE_BINARY_DIR}/component/mbedtls_config
                 EXCLUDE_FROM_ALL)

# Wire the Bouffalo include paths (config + port) onto the upstream
# targets so the config file resolves and the alt headers are visible
# when MBEDTLS_*_ALT macros are on.
#
# Attach the Bouffalo mbedTLS config onto the upstream targets:
#   - mbedtls_cfg (INTERFACE from mbedtls_port): MBEDTLS_USER_CONFIG_FILE + the MBEDTLS_*_ALT
#     selections + the include dir for mbedtls_user_config.h / the *_alt.h headers. It's a pure
#     INTERFACE so it can attach onto upstream without the genex cycle that blocks linking
#     mbedtls_port itself onto mbedcrypto (port reads mbedcrypto's INTERFACE_INCLUDE_DIRECTORIES).
#   - the alt struct headers also need port's SOURCE_DIR on every TU (upstream's <mbedtls/aes.h>
#     #includes "aes_alt.h" when MBEDTLS_AES_ALT is set); fed via SOURCE_DIR (a fixed string,
#     one-way — feeding port's INTERFACE includes back would close the genex cycle).
#   - bflb_lhal: the alt headers #include the SEC driver headers (bflb_sec_aes.h → bflb_core.h →
#     the chip std register headers); link propagation carries bflb_lhal's whole transitive include
#     interface (incl. bl<chip>_std) onto every upstream mbedtls TU. No cycle (bflb_lhal doesn't
#     reference mbedcrypto). everest/p256m need it too (they pull psa/crypto → sha1.h). bflb_lhal
#     is add_subdirectory'd further down — a forward link ref is fine.
foreach(_t mbedcrypto mbedtls mbedx509 everest p256m)
    if(TARGET ${_t})
        target_link_libraries(${_t} PUBLIC mbedtls_cfg bflb_lhal)
        target_include_directories(${_t} PUBLIC
            "$<TARGET_PROPERTY:mbedtls_port,SOURCE_DIR>"
        )
        # MBEDTLS_FS_IO stays on (shared user config) -> x509_crt.c #include <dirent.h>, which the
        # t-head newlib ships as a `#error` stub. component/syscall_config supplies a real
        # <dirent.h>; put its dir FIRST so it shadows the stub. (parse_path is unreferenced here, so
        # it just needs to compile -> dead-strips at link; no opendir/readdir link dep.)
        target_include_directories(${_t} BEFORE PRIVATE
            "${AT_ROOT}/component/syscall_config")
        target_compile_options(${_t} PRIVATE -w)
    endif()
endforeach()

# Swap the bignum module for the Bouffalo PKA fork. Unlike aes/sha/ecp, upstream
# library/bignum.c has no whole-module MBEDTLS_*_ALT guard, so we can't select the HW
# modexp with a macro. Instead: exclude upstream's bignum.c from mbedcrypto (HEADER_FILE_ONLY
# keeps it in the target's file list but tells the generator not to compile it), and let
# mbedtls_port's forked bignum.c + bignum_alt.c (HW mbedtls_mpi_exp_mod) provide the
# mbedtls_mpi_* API instead. TARGET_DIRECTORY sets the property in mbedcrypto's dir scope
# (the source is added there). bignum_core.c / bignum_mod*.c stay upstream.
set_source_files_properties(
    "${CMAKE_CURRENT_LIST_DIR}/../mbedtls_config/mbedtls/library/bignum.c"
    TARGET_DIRECTORY mbedcrypto
    PROPERTIES HEADER_FILE_ONLY ON)
# Utility libs ported one-by-one from
# platform/bouffalo_sdk/components/utils/. All are header-only INTERFACEs
# or small STATIC libs; pull them all in unconditionally — they're tiny
# and consumers opt-in via target_link_libraries.
add_subdirectory(bflb_libc)
add_subdirectory(bflb_getopt)
add_subdirectory(bflb_list)
add_subdirectory(bflb_queue)
add_subdirectory(bflb_tree)
add_subdirectory(bflb_ring_buffer)
add_subdirectory(bflb_math)
add_subdirectory(bflb_coredump)
add_subdirectory(${BOUFFALO_CHIP}_board_config)

# ELF -> flashable BIN pipeline (the bflb_add_executable() helper).
# Lives next to the rest of the SDK so consumers automatically get the
# helper after linking bouffalo_sdk — no separate include() needed.
add_subdirectory(bflb_image_tool)

# phyrf only matters when RF/Wi-Fi is enabled. rfparam (BL602-only in this
# fork — BL616 / BL616CL use a different RF parameter pipeline shipped inside
# their respective phyrf .a archives) needs phyrf's headers, so it gates on
# the same flag.
if(CONFIG_RF)
    add_subdirectory(${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        add_subdirectory(bl602_rfparam)
    endif()
endif()

# wifi4 — Wi-Fi 4 stack. BL602-only (BL616 / BL616CL use wifi6 via the
# wl80211 + macsw + bl_wpa_supplicant trio below). The umbrella here just
# pulls the subtree in; the wifi4 dir's own CMakeLists adds the six
# sublibs and defines the `wifi4` INTERFACE roll-up.
if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_subdirectory(wifi4)
endif()

# Wi-Fi 6 stack on BL616 / BL616CL: three coupled components.
#   wl80211 — Bouffalo's 802.11 driver + wifi_mgmr API surface.
#   macsw   — MAC software layer (prebuilt .a per chip × variant +
#             macsw_plat.c shim).
#   bl_wpa_supplicant — Bouffalo port of wpa_supplicant.
# CONFIG_WIFI6 is the umbrella switch in proj.conf (matches upstream).
# CONFIG_WL80211 + CONFIG_BL_WPA_SUPPLICANT are individually gated to
# mirror upstream's Kconfig but in practice on this fork the three move
# together; we wire them inside the same CONFIG_WIFI6 block.
if(CONFIG_WIFI6 AND ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
                    "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    add_subdirectory(macsw)
    add_subdirectory(bl_wpa_supplicant)
    if(CONFIG_WL80211)
        add_subdirectory(wl80211)
    endif()
endif()

# Generic RF parameter loader for non-BL602 chips (BL602 uses its own
# bl602_rfparam component, wired above). Gated on CONFIG_RF + chip.
if(CONFIG_RF AND NOT ("${BOUFFALO_CHIP}" STREQUAL "bl602"))
    add_subdirectory(rfparam)
endif()

# BLE/BT controller — Bouffalo's chip-specific prebuilt controller .a (the
# Zephyr-derived host + HCI transport were stripped; only the controller +
# its folded-in CONFIG_BT_* defs remain). Gated on CONFIG_BLUETOOTH; BL602 uses
# blecontroller (m1s1 profile), BL616 / BL616CL use btblecontroller.
if(CONFIG_BLUETOOTH)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        add_subdirectory(blecontroller)
    elseif("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
           "${BOUFFALO_CHIP}" STREQUAL "bl616cl")
        add_subdirectory(btblecontroller)
    else()
        message(FATAL_ERROR
            "CONFIG_BLUETOOTH: unsupported BOUFFALO_CHIP '${BOUFFALO_CHIP}' "
            "(blecontroller / btblecontroller .a pruned to bl602/bl616/bl616cl).")
    endif()

    # Apache NimBLE host on top of the on-chip controller (structured HCI).
    if(CONFIG_NIMBLE_HOST)
        add_subdirectory(nimble_port)
    endif()
endif()

add_library(bouffalo_sdk INTERFACE)
target_link_libraries(bouffalo_sdk INTERFACE
    bflb_autoconf
    bflb_sdk_version
    bflb_freertos_port
    ${BOUFFALO_CHIP}_std
    bflb_lhal
    bflb_libc
    tlsf_config
    bflb_log
    bflb_block_pool
    bflb_timestamp
    bflb_partition
    bflb_mtd
    bflb_sys
    bflb_async_event
    ${BOUFFALO_CHIP}_board_config
    # bflb_sysinit comes last so its archive is scanned AFTER bl<chip>_std,
    # which is the only thing that references sysinit_run_all.
    bflb_sysinit
)

# Optional: BL616+ power management (excluded on BL602 — see above).
if(TARGET bflb_power_mgmt)
    target_link_libraries(bouffalo_sdk INTERFACE bflb_power_mgmt)
endif()

# OBJECT libraries don't propagate their object files transitively through an
# INTERFACE intermediary — CMake only auto-extracts $<TARGET_OBJECTS:...> when
# the OBJECT lib is linked directly to a real target. Stitching them in
# explicitly here so symbols kept alive only by linker-script KEEP() rules
# (.fw_header, .version.sdk) actually reach the final ELF.
target_sources(bouffalo_sdk INTERFACE
    $<TARGET_OBJECTS:bflb_sdk_version>
    $<TARGET_OBJECTS:${BOUFFALO_CHIP}_board_config>
)
if(CONFIG_RF)
    target_link_libraries(bouffalo_sdk INTERFACE ${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        target_link_libraries(bouffalo_sdk INTERFACE bl602_rfparam)
    endif()
endif()

if(CONFIG_WIFI4 AND "${BOUFFALO_CHIP}" STREQUAL "bl602")
    target_link_libraries(bouffalo_sdk INTERFACE wifi4)
endif()

if(CONFIG_WIFI6 AND ("${BOUFFALO_CHIP}" STREQUAL "bl616" OR
                    "${BOUFFALO_CHIP}" STREQUAL "bl616cl"))
    target_link_libraries(bouffalo_sdk INTERFACE
        macsw
        bl_wpa_supplicant
    )
    if(TARGET wl80211)
        target_link_libraries(bouffalo_sdk INTERFACE wl80211)
    endif()
endif()

if(TARGET rfparam)
    target_link_libraries(bouffalo_sdk INTERFACE rfparam)
endif()

# Port adapter libs that always get built (bflb_littlefs_port carries headers
# downstream consumers transitively need even when nothing
# references the symbols). Plus the chip's prebuilt BLE/BT controller when
# CONFIG_BLUETOOTH is on. Stitching them in the umbrella here so that downstream projects don't
# have to know which sub-targets exist — they just do
# `target_link_libraries(<exe> PRIVATE bouffalo_sdk)`.
# (mbedtls_port is linked further down, AFTER the upstream mbedtls archives — see there.)
target_link_libraries(bouffalo_sdk INTERFACE
    bflb_littlefs_port
)
# FreeRTOS-Plus-TCP: the wl80211 NetworkInterface bridge + the core stack + its
# BufferAllocation/compiler port. Only defined on the wl80211 chips (bl616/cl);
# all three sit in the --start-group below so the stack<->bridge<->driver symbol
# cycle (wl80211_tcpip_input etc.) resolves at the final image link.
if(TARGET bouffalo_freertos_tcp)
    target_link_libraries(bouffalo_sdk INTERFACE
        bouffalo_freertos_tcp
        freertos_plus_tcp
        freertos_plus_tcp_port
    )
endif()
if(TARGET blecontroller)
    target_link_libraries(bouffalo_sdk INTERFACE blecontroller)
endif()
if(TARGET btblecontroller)
    target_link_libraries(bouffalo_sdk INTERFACE btblecontroller)
endif()
# NimBLE host (pulls its mbedcrypto/mbedtls_config deps + the controller transitively).
if(TARGET nimble)
    target_link_libraries(bouffalo_sdk INTERFACE nimble)
endif()

# Cross-platform third-party libs from component/thirdparty/ (built by its
# own CMakeLists, which knows nothing about Bouffalo) + the upstream mbedtls
# targets pulled in by the upstream-mbedtls block above. Sticking them on the
# umbrella here keeps the dependency one-directional: third-party doesn't peek
# at bouffalo_sdk.
#
# mbedcrypto / mbedtls / mbedx509: the upstream archives. mbedtls_port (which
# now also carries the HACC/PKA MBEDTLS_*_ALT impls, formerly bflb_mbedtls_hw_acc)
# is linked AFTER them ON PURPOSE: those alt impls define the same
# mbedtls_sha*/aes/ecp/bignum symbols as upstream, and the MBEDTLS_*_ALT macros
# don't currently reach the upstream compile (upstream still emits the software
# impls), so within the link group the alt objects must come last — upstream
# resolves the symbols first and the alt members are simply never pulled (they
# act as fallbacks). Linking mbedtls_port before mbedcrypto pulls both and the
# linker reports multiple-definition. The port's platform hooks (entropy /
# timing) still resolve fine via the --start/--end-group re-scan.
target_link_libraries(bouffalo_sdk INTERFACE
    zlibstatic
    cjson
    cjson_utils
    littlefs
    mbedtls
    mbedcrypto
    mbedx509
    mbedtls_port
)

# Bouffalo SDK keeps its -march/-mabi/include flags on the sdk_intf_lib
# INTERFACE target. Without linking against it zlib gets the toolchain's
# default ABI (RV64GC) and bl616 link fails with "ABI is incompatible
# with that of the selected emulation". Wired here, not on zlibstatic's
# own block in thirdparty/, because sdk_intf_lib is a Bouffalo-only
# target — keeping the dependency one-directional.
if(TARGET sdk_intf_lib)
    target_link_libraries(zlibstatic PRIVATE sdk_intf_lib)
endif()

# Open a linker group around the umbrella's transitive archives + force-pull
# SDK's newlib retargets. Closing --end-group is deferred to project scope
# by component/CMakeLists.txt's _bouffalo_sdk_close_link_group() — that's
# the only way to guarantee the `-Wl,--end-group` token is appended past
# every external lib (cjson / mbedtls / …) the umbrella links
# above, since `target_link_options` emits before `target_link_libraries`
# in the final link line.
#
# Reasons for each piece:
#   --start-group: bouffalo_sdk has cyclic refs across archives
#       (bl<chip>_std <-> bflb_sysinit, wifi_manager <-> wpa_supplicant,
#        libc nuttx-stdlib <-> everything pulling memset/strlen,
#        libfirmware.a <-> wifi_manager, …) that single-pass ld can't
#       resolve.
#   -u,_malloc_r etc.: SDK's port_memory.c retargets newlib's _malloc_r /
#       _sbrk_r / friends to kmalloc() (mm subsystem), but it ships as an
#       archive member with no consumer-side reference. Without these -u
#       refs newlib's own _malloc_r (which goes through `ecall SYS_brk`,
#       unserviced on bare metal) wins -> heap_end=0 -> first malloc faults.
#   -Wno-maybe-uninitialized: the toolchain enables -flto on link, so gcc
#       replays compilation of every TU inside the prebuilt vendor archives
#       (libwl80211_*.a / libphyrf_*.a — shipped LTO-fat with jenkins-build
#       paths baked into DWARF). That replay surfaces upstream warnings
#       (cntrl.c / ap.c / scan.c / phy.h) we can't touch. Compile-time
#       -Wmaybe-uninitialized stays on for the project's own sources.
target_link_options(bouffalo_sdk INTERFACE
    -Wl,--start-group
    -Wl,-u,_malloc_r
    -Wl,-u,_free_r
    -Wl,-u,_calloc_r
    -Wl,-u,_realloc_r
    -Wl,-u,_memalign_r
    -Wl,-u,_sbrk_r
    -Wl,-u,_sbrk
    # Force-pull the sprintf/snprintf wrappers from bflb_libc. libstdc++'s
    # cp-demangle.o (linked implicitly after --end-group) references
    # __wrap_sprintf, which the archive scan can't reach back for if nothing
    # earlier in the group referenced it (an LTO-sensitive ordering issue).
    -Wl,-u,__wrap_sprintf
    -Wl,-u,__wrap_snprintf
    -Wno-maybe-uninitialized
)

# Close the --start-group above. `-Wl,--end-group` is smuggled in as a library entry
# (not a link option) so CMake appends it to INTERFACE_LINK_LIBRARIES — emitted AFTER
# the link-options block and after every archive the umbrella transitively pulls. The
# cmake_language(DEFER) to project scope guarantees it is the very LAST entry, past
# every external lib (cjson / mbedtls / the late lfs_easyflash
# self-registration). Lives here with --start-group (was in component/CMakeLists.txt).
function(_bouffalo_sdk_close_link_group)
    target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}
    CALL _bouffalo_sdk_close_link_group)
