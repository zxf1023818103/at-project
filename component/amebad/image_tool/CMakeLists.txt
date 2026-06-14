# AmebaD image-build helper functions.
#
# Wraps the SDK's gnu_utility/ stage-2 pipeline (objcopy → prepend_header.sh
# → cat → optional RSIP via EncTool) as a single CMake function so each
# bootloader CMakeLists.txt can turn its target_loader.axf into the final
# km{0,4}_boot_all.bin with one call.
#
# Source of truth, in the SDK:
#   - project_lp/asdk/Makefile          linker_loader rule, lines 186-225
#   - project_hp/asdk/Makefile          linker_loader rule, lines 499-538
#   - gnu_utility/prepend_header.sh     32-byte header prepend
#   - gnu_utility/image_tool/imagetool.sh + EncTool   RSIP/SBOOT/RDP wrap
#   - security_config.sh                RSIP_KEY / RSIP_IV / *_ENABLE flags
#
# See doc/amebad_image_build.md §二, §三, §五 for the full picture.
#
# imagetool.sh itself is *not* invoked — it hard-codes a `cd
# ./gnu_utility/image_tool` + `source ../../../../security_config.sh` that
# only works from the SDK asdk/ layout. We call EncTool directly with the
# same arguments the script would build for kmX_boot_all.bin.

# === Vendor scripts/binaries. Copied from the SDK into this directory so
# the at-project tree doesn't depend on the platform/ameba-rtos-d submodule
# path resolving for stage 2 — same files, originally at:
#   platform/ameba-rtos-d/.../project_lp/asdk/gnu_utility/prepend_header.sh
#   platform/ameba-rtos-d/.../project_lp/asdk/gnu_utility/image_tool/EncTool
# (project_hp ships byte-identical copies — picked lp arbitrarily). EncTool
# is a closed-source Realtek binary; resync from the submodule when the
# pointer moves and the SDK ships a new revision.
set(AMEBAD_PREPEND_HEADER_SH "${CMAKE_CURRENT_SOURCE_DIR}/prepend_header.sh"
    CACHE INTERNAL "AmebaD prepend_header.sh — 32-byte LE header prepender")
set(AMEBAD_PAD_SH "${CMAKE_CURRENT_SOURCE_DIR}/pad.sh"
    CACHE INTERNAL "AmebaD pad.sh — pad a bin up to the next 4K with 0xFF")
set(AMEBAD_ENCTOOL "${CMAKE_CURRENT_SOURCE_DIR}/EncTool"
    CACHE INTERNAL "AmebaD EncTool (Linux) — RSIP/SBOOT/RDP")

foreach(_p "${AMEBAD_PREPEND_HEADER_SH}" "${AMEBAD_PAD_SH}" "${AMEBAD_ENCTOOL}")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad/image_tool: missing '${_p}'")
    endif()
endforeach()

# Cross-core Image_All.bin assembler (see amebad_make_image_all_bin at the
# bottom of this file). Our own orchestration script + the python to run it.
set(AMEBAD_BUILD_IMAGE_ALL_PY "${CMAKE_CURRENT_SOURCE_DIR}/build_image_all.py"
    CACHE INTERNAL "AmebaD cross-core image assembler (build both cores + lay out)")
if(NOT EXISTS "${AMEBAD_BUILD_IMAGE_ALL_PY}")
    message(FATAL_ERROR "amebad/image_tool: missing '${AMEBAD_BUILD_IMAGE_ALL_PY}'")
endif()
find_program(AMEBAD_PYTHON3 NAMES python3 python)

# === RSIP encryption (optional). Mirrors security_config.sh defaults. The
# SDK also ships with RSIP_ENABLE=0; flip on when you need a flash image
# the SoC will boot with RSIP fused.
#
# RSIP_IV default comes from at-project memory reference_amebad_rsip_iv —
# Realtek FAE-provided. Override per build via -DAMEBAD_RSIP_IV=... if your
# board has a different fused value.
option(AMEBAD_RSIP_ENABLE "Encrypt boot/image bins via RSIP (EncTool)" OFF)
set(AMEBAD_RSIP_KEY "32982100546871a0aa983b2987560000"
    CACHE STRING "RSIP 128-bit key (32 hex chars)")
set(AMEBAD_RSIP_IV  "90acd34ec70a53fa6eba8ca400000000"
    CACHE STRING "RSIP 128-bit IV (32 hex chars)")

# ===========================================================================
# amebad_make_boot_all_bin(<axf-target> <out-name> <rsip-flash-addr>)
#
# Drives the SDK's loader-axf → km{0,4}_boot_all.bin pipeline as a chain of
# POST_BUILD custom commands on <axf-target>. All intermediate + final
# artefacts land in $<TARGET_FILE_DIR:<axf-target>>, mirroring the SDK's
# IMAGE_TARGET_FOLDER convention.
#
# Stages (each is its own add_custom_command so the comments group cleanly
# in build-tool output; POST_BUILD commands on the same target run in
# declaration order):
#
#   A. split axf → ram_1.bin / xip_boot.bin (+ nm map, + asm dump, + pure axf)
#        nm <axf> | sort                            → target_loader.map
#        objdump -d <axf>                           → target_loader.asm
#        cp <axf> target_pure_loader.axf; strip it  (objcopy needs unstripped
#                                                    sections to be present,
#                                                    but the SDK matches its
#                                                    flash layout against the
#                                                    *stripped* axf — keep parity)
#        objcopy -j .ram_image1.{entry,text,data}   → ram_1.bin
#        objcopy -j .xip_image1.text                → xip_boot.bin
#
#   B. prepend 32-byte headers + cat → kmX_boot_all.bin
#        prepend_header.sh ram_1.bin   __ram_start_table_start__  <map>
#        prepend_header.sh xip_boot.bin __flash_boot_text_start__ <map>
#        cat xip_boot_prepend.bin ram_1_prepend.bin > <out-name>
#
#   C. (only when AMEBAD_RSIP_ENABLE) RSIP-encrypt the final bin in place
#        EncTool rsip <out> <out>.enc <rsip-addr> <key> <iv>
#        rename <out>.enc → <out>
#
# Args:
#   AXF_TARGET    Name of the add_executable(...) producing the loader axf
#                 (typically "target_loader" in bootloader_km{0,4}/).
#   OUT_NAME      Basename of the final bin; "km0_boot_all.bin" for KM0,
#                 "km4_boot_all.bin" for KM4.
#   RSIP_ADDR     8-hex flash base used as RSIP entry: 0x08000000 for KM0,
#                 0x08004000 for KM4. Ignored when AMEBAD_RSIP_ENABLE=OFF.
#                 Source: imagetool.sh lines 86-104.
# ===========================================================================
function(amebad_make_boot_all_bin AXF_TARGET OUT_NAME RSIP_ADDR)
    # We avoid $<TARGET_FILE_DIR:...> here because target-dependent genexes
    # are rejected by BYPRODUCTS of the TARGET form of add_custom_command.
    # Inside a function body, ${CMAKE_CURRENT_BINARY_DIR} evaluates against
    # the *caller's* directory scope — i.e. the binary dir of whichever
    # CMakeLists.txt invoked us, which is exactly where the axf is emitted
    # by default (no RUNTIME_OUTPUT_DIRECTORY override in bootloader_km{0,4}).
    set(_imgdir   "${CMAKE_CURRENT_BINARY_DIR}")
    set(_axf      "$<TARGET_FILE:${AXF_TARGET}>")     # genex OK in COMMAND
    set(_axf_pure "${_imgdir}/target_pure_loader.axf")
    set(_nmmap    "${_imgdir}/target_loader.map")     # nm-sorted, not ld -Map
    set(_asmdump  "${_imgdir}/target_loader.asm")
    set(_ram_bin  "${_imgdir}/ram_1.bin")
    set(_xip_bin  "${_imgdir}/xip_boot.bin")
    set(_ram_prep "${_imgdir}/ram_1_prepend.bin")
    set(_xip_prep "${_imgdir}/xip_boot_prepend.bin")
    set(_out_bin  "${_imgdir}/${OUT_NAME}")

    # --- Stage A: split axf -> ram_1.bin / xip_boot.bin (+ nm map, + asm, + pure axf)
    #
    # NB: prepend_header.sh uses `grep $SYMBOL $SYMBOL_LIST | awk '{print $1}'`
    # which expects the *nm-sorted* output ("addr type name" per line), NOT
    # the ld -Wl,-Map=... output that bootloader_km{0,4}/CMakeLists.txt
    # already emits as target_loader.map. We overwrite that file here with
    # the nm-format map the prepend script needs — same name as the SDK
    # produces in IMAGE_TARGET_FOLDER.
    add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
        COMMAND bash -c "'${CMAKE_NM}' '${_axf}' | sort > '${_nmmap}'"
        COMMAND bash -c "'${CMAKE_OBJDUMP}' -d '${_axf}' > '${_asmdump}'"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_axf}" "${_axf_pure}"
        COMMAND "${CMAKE_STRIP}" "${_axf_pure}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .ram_image1.entry
                -j .ram_image1.text
                -j .ram_image1.data
                -O binary "${_axf_pure}" "${_ram_bin}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .xip_image1.text
                -O binary "${_axf_pure}" "${_xip_bin}"
        BYPRODUCTS
            "${_nmmap}" "${_asmdump}" "${_axf_pure}"
            "${_ram_bin}" "${_xip_bin}"
        COMMENT "amebad: split ${AXF_TARGET} -> ram_1.bin / xip_boot.bin"
        VERBATIM
    )

    # --- Stage B: prepend 32-byte headers + cat into kmX_boot_all.bin
    add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_ram_bin}" __ram_start_table_start__  "${_nmmap}"
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_xip_bin}" __flash_boot_text_start__  "${_nmmap}"
        COMMAND bash -c "cat '${_xip_prep}' '${_ram_prep}' > '${_out_bin}'"
        BYPRODUCTS "${_ram_prep}" "${_xip_prep}" "${_out_bin}"
        COMMENT "amebad: prepend headers + cat -> ${OUT_NAME}"
        VERBATIM
    )

    # --- Stage C: optional RSIP encryption in place.
    if(AMEBAD_RSIP_ENABLE)
        set(_out_enc "${_imgdir}/${OUT_NAME}.enc")
        add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
            COMMAND "${AMEBAD_ENCTOOL}" rsip
                    "${_out_bin}" "${_out_enc}" "${RSIP_ADDR}"
                    "${AMEBAD_RSIP_KEY}" "${AMEBAD_RSIP_IV}"
            COMMAND "${CMAKE_COMMAND}" -E rename "${_out_enc}" "${_out_bin}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            BYPRODUCTS "${_out_enc}"
            COMMENT "amebad: RSIP-encrypt ${OUT_NAME} @ ${RSIP_ADDR}"
            VERBATIM
        )
    endif()
endfunction()

# ===========================================================================
# amebad_make_image2_all_bin(<axf-target> <out-name> <rsip-xip-addr>)
#
# image2 (application) counterpart of amebad_make_boot_all_bin. Drives the
# SDK's img2-axf -> km0_image2_all.bin pipeline. Differences vs. the loader:
#   - three segments instead of two (+ .ram_retention),
#   - image2 prepend symbols (__ram_image2_text_start__ / __flash_text_start__
#     / __retention_entry_func__),
#   - a 4 KB pad after cat (pad.sh),
#   - RSIP entry is the XIP *cache* base (0x0c000000 for KM0), not the flash
#     base the loader uses.
#
# Source of truth: project_lp/asdk/Makefile, the `image2:` postlink block
# (lines 132-176) + gnu_utility/{prepend_header,pad}.sh + image_tool/
# imagetool.sh (RSIP @ 0x0c000000, line 120).
#
# NB the SDK prepends ram_retention.bin but does NOT cat it into
# km0_image2_all.bin (cat is xip_image2_prepend + ram_2_prepend only) — we
# match that exactly; ram_retention_prepend.bin is left as a byproduct.
#
# Args:
#   AXF_TARGET    add_executable() producing the image2 axf (target_img2_km0).
#   OUT_NAME      final bin basename (km0_image2_all.bin).
#   RSIP_ADDR     XIP cache base for RSIP: 0x0c000000 (KM0). RSIP-only.
# ===========================================================================
function(amebad_make_image2_all_bin AXF_TARGET OUT_NAME RSIP_ADDR)
    set(_imgdir   "${CMAKE_CURRENT_BINARY_DIR}")
    set(_axf      "$<TARGET_FILE:${AXF_TARGET}>")
    set(_axf_pure "${_imgdir}/target_pure_img2.axf")
    set(_nmmap    "${_imgdir}/target_img2.map")          # nm-sorted, for prepend
    set(_asmdump  "${_imgdir}/target_img2.asm")
    set(_ram_bin  "${_imgdir}/ram_2.bin")
    set(_xip_bin  "${_imgdir}/xip_image2.bin")
    set(_ret_bin  "${_imgdir}/ram_retention.bin")
    set(_ram_prep "${_imgdir}/ram_2_prepend.bin")
    set(_xip_prep "${_imgdir}/xip_image2_prepend.bin")
    set(_ret_prep "${_imgdir}/ram_retention_prepend.bin")
    set(_out_bin  "${_imgdir}/${OUT_NAME}")

    # --- Stage A: split axf -> ram_2 / xip_image2 / ram_retention (+ nm map, asm, pure axf)
    add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
        COMMAND bash -c "'${CMAKE_NM}' '${_axf}' | sort > '${_nmmap}'"
        COMMAND bash -c "'${CMAKE_OBJDUMP}' -d '${_axf}' > '${_asmdump}'"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_axf}" "${_axf_pure}"
        COMMAND "${CMAKE_STRIP}" "${_axf_pure}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .ram_image2.entry
                -j .ram_image2.text
                -j .ram_image2.data
                -O binary "${_axf_pure}" "${_ram_bin}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .xip_image2.text
                -O binary "${_axf_pure}" "${_xip_bin}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .ram_retention.text
                -j .ram_retention.entry
                -O binary "${_axf_pure}" "${_ret_bin}"
        BYPRODUCTS
            "${_nmmap}" "${_asmdump}" "${_axf_pure}"
            "${_ram_bin}" "${_xip_bin}" "${_ret_bin}"
        COMMENT "amebad: split ${AXF_TARGET} -> ram_2 / xip_image2 / ram_retention"
        VERBATIM
    )

    # --- Stage B: prepend 32-byte headers (all three), cat xip+ram_2, 4K pad.
    add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_ram_bin}" __ram_image2_text_start__   "${_nmmap}"
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_xip_bin}" __flash_text_start__        "${_nmmap}"
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_ret_bin}" __retention_entry_func__    "${_nmmap}"
        COMMAND bash -c "cat '${_xip_prep}' '${_ram_prep}' > '${_out_bin}'"
        COMMAND bash "${AMEBAD_PAD_SH}" "${_out_bin}"
        BYPRODUCTS "${_ram_prep}" "${_xip_prep}" "${_ret_prep}" "${_out_bin}"
        COMMENT "amebad: prepend + cat + 4K pad -> ${OUT_NAME}"
        VERBATIM
    )

    # --- Stage C: optional RSIP encryption in place (XIP cache base).
    if(AMEBAD_RSIP_ENABLE)
        set(_out_enc "${_imgdir}/${OUT_NAME}.enc")
        add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
            COMMAND "${AMEBAD_ENCTOOL}" rsip
                    "${_out_bin}" "${_out_enc}" "${RSIP_ADDR}"
                    "${AMEBAD_RSIP_KEY}" "${AMEBAD_RSIP_IV}"
            COMMAND "${CMAKE_COMMAND}" -E rename "${_out_enc}" "${_out_bin}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            BYPRODUCTS "${_out_enc}"
            COMMENT "amebad: RSIP-encrypt ${OUT_NAME} @ ${RSIP_ADDR}"
            VERBATIM
        )
    endif()
endfunction()

# ===========================================================================
# amebad_make_image2_all_bin_hp(<axf-target> <out-name> <rsip-xip-addr>)
#
# KM4 (HP) image2 packaging. Like the KM0 amebad_make_image2_all_bin but the
# third segment is PSRAM, not retention, and — unlike KM0's ram_retention —
# it IS cat'd into the final bin:
#   ram_2       .ram_image2.entry/text/data    prepend __ram_image2_text_start__
#   xip_image2  .xip_image2.text               prepend __flash_text_start__
#   psram_2     .psram_image2.text/data        prepend __psram_image2_text_start__
#   cat: xip_image2_prepend + ram_2_prepend + psram_2_prepend -> km4_image2_all.bin
#   pad.sh (4 KB); RSIP (optional) @ the KM4 XIP cache base 0x0e000000.
#
# (For the minimal no-PSRAM firmware the .psram_image2 sections are empty, so
# psram_2 is just a 32-byte header — harmless, matches the SDK.)
#
# Source of truth: project_hp/asdk/Makefile image2_is postlink (lines 447-485)
# + image_tool/imagetool.sh (RSIP @ 0x0e000000).
# ===========================================================================
function(amebad_make_image2_all_bin_hp AXF_TARGET OUT_NAME RSIP_ADDR)
    set(_imgdir   "${CMAKE_CURRENT_BINARY_DIR}")
    set(_axf      "$<TARGET_FILE:${AXF_TARGET}>")
    set(_axf_pure "${_imgdir}/target_pure_img2.axf")
    set(_nmmap    "${_imgdir}/target_img2.map")
    set(_asmdump  "${_imgdir}/target_img2.asm")
    set(_ram_bin  "${_imgdir}/ram_2.bin")
    set(_xip_bin  "${_imgdir}/xip_image2.bin")
    set(_psr_bin  "${_imgdir}/psram_2.bin")
    set(_ram_prep "${_imgdir}/ram_2_prepend.bin")
    set(_xip_prep "${_imgdir}/xip_image2_prepend.bin")
    set(_psr_prep "${_imgdir}/psram_2_prepend.bin")
    set(_out_bin  "${_imgdir}/${OUT_NAME}")

    # --- Stage A: split axf -> ram_2 / xip_image2 / psram_2 (+ nm map, asm, pure axf)
    add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
        COMMAND bash -c "'${CMAKE_NM}' '${_axf}' | sort > '${_nmmap}'"
        COMMAND bash -c "'${CMAKE_OBJDUMP}' -d '${_axf}' > '${_asmdump}'"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_axf}" "${_axf_pure}"
        COMMAND "${CMAKE_STRIP}" "${_axf_pure}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .ram_image2.entry -j .ram_image2.text -j .ram_image2.data
                -O binary "${_axf_pure}" "${_ram_bin}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .xip_image2.text
                -O binary "${_axf_pure}" "${_xip_bin}"
        COMMAND "${CMAKE_OBJCOPY}"
                -j .psram_image2.text -j .psram_image2.data
                -O binary "${_axf_pure}" "${_psr_bin}"
        BYPRODUCTS
            "${_nmmap}" "${_asmdump}" "${_axf_pure}"
            "${_ram_bin}" "${_xip_bin}" "${_psr_bin}"
        COMMENT "amebad: split ${AXF_TARGET} -> ram_2 / xip_image2 / psram_2"
        VERBATIM
    )

    # --- Stage B: prepend 32-byte headers (all three), cat xip+ram_2+psram_2, 4K pad.
    add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_ram_bin}" __ram_image2_text_start__   "${_nmmap}"
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_xip_bin}" __flash_text_start__        "${_nmmap}"
        COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
                "${_psr_bin}" __psram_image2_text_start__ "${_nmmap}"
        COMMAND bash -c "cat '${_xip_prep}' '${_ram_prep}' '${_psr_prep}' > '${_out_bin}'"
        COMMAND bash "${AMEBAD_PAD_SH}" "${_out_bin}"
        BYPRODUCTS "${_ram_prep}" "${_xip_prep}" "${_psr_prep}" "${_out_bin}"
        COMMENT "amebad: prepend + cat + 4K pad -> ${OUT_NAME}"
        VERBATIM
    )

    # --- Stage C: optional RSIP encryption in place (KM4 XIP cache base).
    if(AMEBAD_RSIP_ENABLE)
        set(_out_enc "${_imgdir}/${OUT_NAME}.enc")
        add_custom_command(TARGET ${AXF_TARGET} POST_BUILD
            COMMAND "${AMEBAD_ENCTOOL}" rsip
                    "${_out_bin}" "${_out_enc}" "${RSIP_ADDR}"
                    "${AMEBAD_RSIP_KEY}" "${AMEBAD_RSIP_IV}"
            COMMAND "${CMAKE_COMMAND}" -E rename "${_out_enc}" "${_out_bin}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            BYPRODUCTS "${_out_enc}"
            COMMENT "amebad: RSIP-encrypt ${OUT_NAME} @ ${RSIP_ADDR}"
            VERBATIM
        )
    endif()
endfunction()

# ===========================================================================
# amebad_make_image_all_bin([OUT <path>])
#
# Cross-core final step. The per-core packaging functions above run as
# POST_BUILD on a single axf and produce km{0,4}_boot_all.bin /
# km{0,4}_image2_all.bin within one build tree. The flashable Image_All.bin,
# though, needs BOTH cores' outputs — and KM0 / KM4 are independent CMake build
# trees (different toolchain files), so the assembly can't live inside a
# single-core build. That orchestration is build_image_all.py: it configures +
# builds both trees (driving the per-core POST_BUILD packaging above),
# cross-core-merges the image2 halves, and lays km0_boot / km4_boot /
# km0_km4_image2 into Image_All.bin.
#
# This wraps that script as the standalone `amebad_image_all` custom target:
#
#       cmake --build <km0-or-km4-build-dir> -t amebad_image_all
#
# The target has no dependencies in the current tree — building it shells out to
# build_image_all.py, which (re)builds both cores and assembles. Use this
# instead of invoking build_image_all.py by hand. OUT overrides the script's
# default output (build/amebad/Image_All.bin under the repo root).
# ===========================================================================
function(amebad_make_image_all_bin)
    cmake_parse_arguments(_img "" "OUT" "" ${ARGN})

    if(NOT AMEBAD_PYTHON3)
        message(FATAL_ERROR
            "amebad_make_image_all_bin: python3 not found (needed to run "
            "${AMEBAD_BUILD_IMAGE_ALL_PY})")
    endif()

    set(_args)
    if(_img_OUT)
        list(APPEND _args --out "${_img_OUT}")
    endif()

    # One target per build tree; guard against a double call in the same configure.
    if(NOT TARGET amebad_image_all)
        add_custom_target(amebad_image_all
            COMMAND "${AMEBAD_PYTHON3}" "${AMEBAD_BUILD_IMAGE_ALL_PY}" ${_args}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            USES_TERMINAL
            COMMENT "amebad: build both cores + assemble Image_All.bin (build_image_all.py)"
            VERBATIM
        )
    endif()
endfunction()
