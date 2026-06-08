# component/amebad/app_km4_secure — AmebaD KM4 secure-world firmware image (IMG3).
#
# The secure counterpart to app_km4 (KM4 HP non-secure image2). Two secure-world
# variants live here, each behind its own option:
#
#   AMEBAD_ENABLE_TZ_DEMO  -> the in-tree RDP-style TrustZone-M demo (main.c),
#                             target target_img3_km4_secure.
#   AMEBAD_TFM_BOOT        -> the TF-M variant: the NSPE bring-up stub (ns_stub)
#                             is built here; the SPE blob tfm_s.bin is produced
#                             OUT-OF-TREE by the TF-M build (platform port under
#                             platform/rtl8721d) and supplied into the build dir.
#
# Build artifacts (ns_stub.bin / tfm_s.bin / the IMG3 blobs) are emitted into the
# build tree (build/km4/...), never the source dir — source and generated files
# are kept strictly apart. build_image_all.py reads them from the build dir.
#
# Standalone secure blobs: no crt/startup, no ROM lib, no libc.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

if(NOT AMEBAD_ENABLE_TZ_DEMO AND NOT AMEBAD_TFM_BOOT)
    return()
endif()

# =====================================================================
# RDP-style secure-world demo (target target_img3_km4_secure)
# =====================================================================
# Builds target_img3_km4_secure (.axf) and splits it — mirroring
# project_hp/asdk/Makefile linker_image3 — into:
#   ram_3_s.bin    .ram_image3.text/.data  -> bootloader loads to BD_RAM_S  @0x10077000
#   ram_3_nsc.bin  .gnu.sgstubs (SG stubs) -> bootloader loads to BD_RAM_NSC @0x1007B000
#   cmse_implib.lib  the CMSE import library (the NS image2 links this to resolve
#                    the veneer addresses for rdp_protection_entry/BOOT_IMG3).
# It is entered only through its cmse_nonsecure_entry veneers (the bootloader sets MSP_S).
if(AMEBAD_ENABLE_TZ_DEMO)
    set(_img3_ld   "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_img3_s.ld")
    set(_layout_ld "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout_tz.ld")

    add_executable(target_img3_km4_secure
        "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
    )

    # Freestanding secure blob: stop GCC turning the .bss-zeroing loop into a call
    # to libc memset (we link -nostdlib, there is no memset to resolve).
    target_compile_options(target_img3_km4_secure PRIVATE -fno-tree-loop-distribute-patterns)

    # -mcmse / -march=armv8-m.main+dsp come from the KM4 toolchain file globally.
    target_link_options(target_img3_km4_secure PRIVATE
        "-T${_img3_ld}"
        "-L${CMAKE_CURRENT_SOURCE_DIR}"          # resolve INCLUDE "rlx8721d_layout_tz.ld"
        "-nostartfiles"
        "-nostdlib"
        "-Wl,--cmse-implib"
        "-Wl,--out-implib=${CMAKE_CURRENT_BINARY_DIR}/cmse_implib.lib"
        "-Wl,-Map=$<TARGET_FILE_DIR:target_img3_km4_secure>/target_img3_km4_secure.map"
    )
    set_target_properties(target_img3_km4_secure PROPERTIES
        LINK_DEPENDS "${_img3_ld};${_layout_ld}"
        EXCLUDE_FROM_ALL TRUE)

    # Split the secure axf into the two flashable blobs the bootloader expects.
    set(_imgdir   "${CMAKE_CURRENT_BINARY_DIR}")
    set(_axf      "$<TARGET_FILE:target_img3_km4_secure>")
    set(_axf_pure "${_imgdir}/target_img3_km4_secure_pure.axf")
    add_custom_command(TARGET target_img3_km4_secure POST_BUILD
        COMMAND bash -c "'${CMAKE_NM}' '${_axf}' | sort > '${_imgdir}/target_img3_km4_secure.symbols'"
        COMMAND bash -c "'${CMAKE_OBJDUMP}' -d '${_axf}' > '${_imgdir}/target_img3_km4_secure.asm'"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_axf}" "${_axf_pure}"
        COMMAND "${CMAKE_STRIP}" "${_axf_pure}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .ram_image3.text -j .ram_image3.data
                -O binary "${_axf_pure}" "${_imgdir}/ram_3_s.bin"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .gnu.sgstubs
                -O binary "${_axf_pure}" "${_imgdir}/ram_3_nsc.bin"
        COMMAND "${CMAKE_SIZE}" -A --radix=16 "${_axf}"
        BYPRODUCTS
            "${_imgdir}/target_img3_km4_secure.symbols" "${_imgdir}/target_img3_km4_secure.asm"
            "${_axf_pure}" "${_imgdir}/ram_3_s.bin" "${_imgdir}/ram_3_nsc.bin"
        COMMENT "amebad: split target_img3_km4_secure -> ram_3_s.bin / ram_3_nsc.bin (+ cmse_implib.lib)"
        VERBATIM
    )

    # --- Package: prepend 32-byte headers (Realtek IMG2SIGN + size + load addr from
    # the nm symbol dump) to each blob, then cat -> km4_image3_all.bin. The
    # bootloader's BOOT_FLASH_Image3Load walks: [hdr|ram_3_s]@0x10077000 then
    # [hdr|ram_3_nsc]@0x1007B000. (Mirrors project_hp/asdk/Makefile linker_image3.)
    add_custom_command(TARGET target_img3_km4_secure POST_BUILD
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_imgdir}/ram_3_s.bin"   __ram_image3_text_start__ "${_imgdir}/target_img3_km4_secure.symbols"
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_imgdir}/ram_3_nsc.bin" __ram_image3_nsc_start__  "${_imgdir}/target_img3_km4_secure.symbols"
        COMMAND bash -c "cat '${_imgdir}/ram_3_s_prepend.bin' '${_imgdir}/ram_3_nsc_prepend.bin' > '${_imgdir}/km4_image3_all.bin'"
        BYPRODUCTS
            "${_imgdir}/ram_3_s_prepend.bin" "${_imgdir}/ram_3_nsc_prepend.bin"
            "${_imgdir}/km4_image3_all.bin"
        COMMENT "amebad: prepend headers + cat -> km4_image3_all.bin"
        VERBATIM
    )
endif()

# =====================================================================
# TF-M NSPE bring-up stub (ns_stub.bin)
# =====================================================================
# A tiny RAM-resident Non-Secure stub: TF-M's SPM BLXNS-es to it after secure init
# to prove the S->NS handoff on HW. Built freestanding from ns_stub/ and objcopy'd
# to ns_stub.bin in the build dir (was previously a committed .bin — now generated).
# The TF-M SPE blob tfm_s.bin is produced by the out-of-tree TF-M build and dropped
# into this target's build dir; build_image_all.py reads both from there.
if(AMEBAD_TFM_BOOT)
    add_executable(ns_stub "${CMAKE_CURRENT_SOURCE_DIR}/ns_stub/ns_stub.c")
    target_compile_options(ns_stub PRIVATE -fno-tree-loop-distribute-patterns)
    target_link_options(ns_stub PRIVATE
        "-T${CMAKE_CURRENT_SOURCE_DIR}/ns_stub/ns_stub.ld"
        "-nostartfiles"
        "-nostdlib"
        "-Wl,-Map=$<TARGET_FILE_DIR:ns_stub>/ns_stub.map"
    )
    set_target_properties(ns_stub PROPERTIES
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ns_stub/ns_stub.ld"
        EXCLUDE_FROM_ALL TRUE)
    add_custom_command(TARGET ns_stub POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:ns_stub>" "${CMAKE_CURRENT_BINARY_DIR}/ns_stub.bin"
        COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:ns_stub>"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/ns_stub.bin"
        COMMENT "amebad: objcopy ns_stub -> ns_stub.bin"
        VERBATIM
    )
endif()
