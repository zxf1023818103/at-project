# bflb_sys — system-level driver layer: per-chip sys.c + (for BL616/BL616CL)
# the low-power state machinery (LP wakeup/callback, XIP recovery, beacon
# tracking, etc.).
#
# Extracted from bouffalo_sdk/drivers/sys. The chip-conditional branches for
# bl702/bl702l have been dropped (only bl602/bl616/bl616cl/bl618dg carried
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
elseif("${BOUFFALO_CHIP}" STREQUAL "bl618dg")
    # AP and NP build the identical set (verified against an SDK dualcore build).
    # Note bl618dg has no *_ext_dcdc.c / *_psram_recovery.c counterpart.
    add_library(bflb_sys STATIC
        bl618dg/bl618dg_sys.c
        bl_lp_callback.c
        bl_lp_bcn.c
        bl_pm_wifi.c
        bl_lp_fw_image.c
        bl618dg/bl618dg_lp.c
        bl618dg/bl618dg_lp_callback.c
        bl618dg/bl618dg_lp_wakeup.c
        bl618dg/bl618dg_lp_bcn_tbl.c
        bl618dg/bl618dg_app_pds_recovery.c
        bl618dg/bl618dg_xip_recovery.c
        bl618dg/bl618dg_32k_trim.c
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

# LP_FW=ON (CONFIG_LPAPP): make bl_lp_init() also run bl_lp_fw_init() — it validates the concatenated
# lp_fw blob and loads it to RAM + fully populates iot2lp_para (shared_func_array / wifi_parameter /
# bcn_delay / rc32k / mtimer). Without this CFG_BL_WIFI_PS_ENABLE gate bl_lp_init() only allocates
# gp_lp_env. The macro solely toggles that one call in bl616_lp.c (no struct-layout effect). The
# app-side lp_fw shared functions it pulls (lpfw_calculate_beacon_delay / ...) come from the vendored
# liblpfw_private.a, which the fhost component links under CONFIG_LPAPP.
if(CONFIG_LPAPP)
    target_compile_definitions(bflb_sys PRIVATE CFG_BL_WIFI_PS_ENABLE=1)
endif()
