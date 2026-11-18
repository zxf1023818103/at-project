/*
 * mcumgr_reset_bflb.c - bl602 glue for the platform-agnostic mcumgr img/os impls.
 *
 * smp_img_ota.c (shared with AmebaD) reboots via sys_reset(), which on AmebaD is the mbed sys_api.c
 * WDG reset. bl602 has no sys_reset(); map it to the bl602 full power-on reset (bflb_sys). This is an
 * IMMEDIATE reset (does not return), unlike AmebaD's deferred WDG window - so the final mcumgr
 * upload/os-reset SMP response may not flush over BLE before the reboot fires (the image is still
 * written + armed correctly by ota_flash_finish before sys_reset() is called).
 */
extern int bl_sys_reset_por(void);   /* bflb_sys: full power-on reset (resolves at the image link) */

void sys_reset(void)
{
    bl_sys_reset_por();
    for (;;) { }                     /* not reached; keep the no-return contract smp_img_ota.c expects */
}
