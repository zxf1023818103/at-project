# bflb_sys — system-level driver layer: per-chip sys.c + (for BL616/BL616CL)
# the low-power state machinery (LP wakeup/callback, XIP recovery, beacon
# tracking, etc.).
#
# Extracted from bouffalo_sdk/drivers/sys. The chip-conditional branches for
# bl702/bl702l/bl618dg have been dropped (only bl602/bl616/bl616cl carried
# over per the pruning policy).

if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    add_library(bflb_sys STATIC
        bl602/bl602_sys.c
    )
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616")
    add_library(bflb_sys STATIC
        bl616/bl616_sys.c
        bl_lp_callback.c
        bl_lp_bcn.c
        bl_pm_wifi.c
        bl_lp_fw_image.c
        bl616/bl616_lp.c
        bl616/bl616_lp_callback.c
        bl616/bl616_lp_fw_image.c
        bl616/bl616_lp_wakeup.c
        bl616/bl616_lp_wifi.c
        bl616/bl616_xip_recovery.c
        bl616/bl616_32k_trim.c
    )
elseif("${BOUFFALO_CHIP}" STREQUAL "bl616cl")
    add_library(bflb_sys STATIC
        bl616cl/bl616cl_sys.c
        bl_lp_callback.c
        bl_lp_bcn.c
        bl_pm_wifi.c
        bl_lp_fw_image.c
        bl616cl/bl616cl_lp.c
        bl616cl/bl616cl_ext_dcdc.c
        bl616cl/bl616cl_lp_callback.c
        bl616cl/bl616cl_lp_wakeup.c
        bl616cl/bl616cl_lp_bcn_tbl.c
        bl616cl/bl616cl_app_pds_recovery.c
        bl616cl/bl616cl_psram_recovery.c
        bl616cl/bl616cl_xip_recovery.c
        bl616cl/bl616cl_32k_trim.c
    )
else()
    message(FATAL_ERROR "bflb_sys: unsupported BOUFFALO_CHIP '${BOUFFALO_CHIP}'")
endif()

target_include_directories(bflb_sys PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${BOUFFALO_CHIP}"
)

# bl<chip>_sys.c includes chip register headers (bl<chip>_glb.h, bl<chip>_romdriver.h)
# from bl<chip>_std, plus bflb_common.h / bflb_sec_sha.h from lhal (covered
# transitively through std's PUBLIC link to lhal).
target_link_libraries(bflb_sys PUBLIC ${BOUFFALO_CHIP}_std)
