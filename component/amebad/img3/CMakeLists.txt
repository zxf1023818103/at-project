# AmebaD KM4 secure-world image (IMG3) — minimal RDP-style TrustZone-M demo.
#
# Produces target_img3_km4.axf and splits it (mirroring project_hp/asdk/Makefile
# linker_image3) into:
#   ram_3_s.bin    .ram_image3.text/.data  -> loaded by the bootloader to
#                                              BD_RAM_S  @0x10077000
#   ram_3_nsc.bin  .gnu.sgstubs (SG stubs) -> loaded to BD_RAM_NSC @0x1007B000
#   cmse_implib.lib  the CMSE import library (the NS image links this to resolve
#                    the veneer addresses for rdp_protection_entry/BOOT_IMG3).
#
# Opt-in (off by default): -DAMEBAD_ENABLE_TZ_DEMO=ON. The KM4 image2 (NS) build
# links cmse_implib.lib and calls the veneers; the bootloader must be built with
# configENABLE_TRUSTZONE=1 so BOOT_FLASH_Image3Load() copies these blobs in.
#
# Standalone secure blob: no crt/startup, no ROM lib — it is only ever entered
# through its cmse_nonsecure_entry veneers (the bootloader sets MSP_S).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

if(NOT AMEBAD_ENABLE_TZ_DEMO)   # option defined in component/amebad/CMakeLists.txt
    return()
endif()

set(_img3_ld   "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_img3_s.ld")
set(_layout_ld "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_layout_tz.ld")

add_executable(target_img3_km4
    "${CMAKE_CURRENT_SOURCE_DIR}/rdp_secure.c"
)

# Freestanding secure blob: stop GCC turning the .bss-zeroing loop into a call to
# libc memset (we link -nostdlib, there is no memset to resolve).
target_compile_options(target_img3_km4 PRIVATE -fno-tree-loop-distribute-patterns)

# -mcmse / -march=armv8-m.main+dsp come from the KM4 toolchain file globally.
# The secure image is freestanding: no crt0, no libc, no ROM symbols.
target_link_options(target_img3_km4 PRIVATE
    "-T${_img3_ld}"
    "-L${CMAKE_CURRENT_SOURCE_DIR}"          # resolve INCLUDE "rlx8721d_layout_tz.ld"
    "-nostartfiles"
    "-nostdlib"
    "-Wl,--cmse-implib"
    "-Wl,--out-implib=${CMAKE_CURRENT_BINARY_DIR}/cmse_implib.lib"
    "-Wl,-Map=$<TARGET_FILE_DIR:target_img3_km4>/target_img3.map"
)
set_target_properties(target_img3_km4 PROPERTIES
    LINK_DEPENDS "${_img3_ld};${_layout_ld}")

set_target_properties(target_img3_km4 PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Split the secure axf into the two flashable blobs the bootloader expects.
set(_imgdir   "${CMAKE_CURRENT_BINARY_DIR}")
set(_axf      "$<TARGET_FILE:target_img3_km4>")
set(_axf_pure "${_imgdir}/target_pure_img3.axf")
add_custom_command(TARGET target_img3_km4 POST_BUILD
    COMMAND bash -c "'${CMAKE_NM}' '${_axf}' | sort > '${_imgdir}/target_img3.map'"
    COMMAND bash -c "'${CMAKE_OBJDUMP}' -d '${_axf}' > '${_imgdir}/target_img3.asm'"
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
        "${_imgdir}/target_img3.map" "${_imgdir}/target_img3.asm"
        "${_axf_pure}" "${_imgdir}/ram_3_s.bin" "${_imgdir}/ram_3_nsc.bin"
    COMMENT "amebad: split target_img3_km4 -> ram_3_s.bin / ram_3_nsc.bin (+ cmse_implib.lib)"
    VERBATIM
)

# --- Package: prepend 32-byte headers (Realtek IMG2SIGN + size + load addr from
# the nm map) to each blob, then cat -> km4_image3_all.bin. The bootloader's
# BOOT_FLASH_Image3Load walks: [hdr|ram_3_s]@0x10077000 then [hdr|ram_3_nsc]@
# 0x1007B000. (Mirrors project_hp/asdk/Makefile linker_image3 lines 380-385.)
add_custom_command(TARGET target_img3_km4 POST_BUILD
    COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
            "${_imgdir}/ram_3_s.bin"   __ram_image3_text_start__ "${_imgdir}/target_img3.map"
    COMMAND bash "${AMEBAD_PREPEND_HEADER_SH}"
            "${_imgdir}/ram_3_nsc.bin" __ram_image3_nsc_start__  "${_imgdir}/target_img3.map"
    COMMAND bash -c "cat '${_imgdir}/ram_3_s_prepend.bin' '${_imgdir}/ram_3_nsc_prepend.bin' > '${_imgdir}/km4_image3_all.bin'"
    BYPRODUCTS
        "${_imgdir}/ram_3_s_prepend.bin" "${_imgdir}/ram_3_nsc_prepend.bin"
        "${_imgdir}/km4_image3_all.bin"
    COMMENT "amebad: prepend headers + cat -> km4_image3_all.bin"
    VERBATIM
)
