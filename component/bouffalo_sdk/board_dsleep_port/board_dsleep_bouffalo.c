/*
 * board_dsleep_bouffalo.c - Bouffalo (BL616 / BL616CL / BL602) impl of board_dsleep_enter() for the
 * shared `dsleep [uart] [rtc <seconds>]` command (component/dsleep_config, dsleep.h). All chips do a
 * full PDS deep sleep that RESETS the chip on wake; the wake sources are a UART-RX pad edge and/or
 * the PDS 32 kHz timer. The PDS enter API diverges between the chips, so this is #if-split.
 */
#include "dsleep.h"

#if defined(BL616)

#include "bl616_pds.h"   /* PDS_* regs/enums, pm_pds_* via bl616_pm.h, BL_RD/WR_REG, PDS_BASE */
#include "bl616_hbn.h"   /* HBN_Pin_WakeUp_Mask */
#include "bl616_aon.h"   /* AON_Output_Float_LDO15_RF */
#include "bl616_pm.h"    /* pm_pds_mask_all_wakeup_src / pm_pds_wakeup_src_en / pm_pds_mode_enter */

int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds)
{
    /* Same PDS15 bring-down as the bare `pdssleep`. */
    pm_pds_mask_all_wakeup_src();
    AON_Output_Float_LDO15_RF();
    HBN_Pin_WakeUp_Mask(0xF);

    if (uart_wake) {
        /* Console RX = GPIO22 (bit 22 -> PDS_GPIO_INT_SET_3_GPIO20_GPIO27). Arm a PDS-GPIO async
         * falling-edge wake on it (UART idles high; the start bit is a high->low edge). Mirrors the
         * SDK lp_fw IO-wakeup reference (bflb_sys/bl616/bl616_lp_wakeup.c:256-275) with a single
         * unmasked pin (io_unmask = 1<<22), pull-up so the idle level stays high. */
        const uint32_t io_unmask = (1u << 22);
        PDS_Set_GPIO_Pad_Pn_Pu_Pd_Ie(PDS_GPIO_GROUP_SET_GPIO20_GPIO36, 1 /*pu*/, 0 /*pd*/, 1 /*ie*/);
        PDS_Set_GPIO_Pad_IntClr(PDS_GPIO_INT_SET_3_GPIO20_GPIO27);
        PDS_Set_GPIO_Pad_IntMode(PDS_GPIO_INT_SET_3_GPIO20_GPIO27, PDS_GPIO_INT_ASYNC_FALLING_EDGE);
        uint32_t pd = BL_RD_REG(PDS_BASE, PDS_GPIO_PD_SET);
        pd &= ~0xFFFF0000u;
        pd |= ((((~io_unmask) >> 20) & 0x7FFFu) << 16);   /* mask bit=1 per pin; GPIO22 stays 0=unmasked */
        BL_WR_REG(PDS_BASE, PDS_GPIO_PD_SET, pd);
        pm_pds_wakeup_src_en(PDS_WAKEUP_BY_PDS_GPIO_IRQ_EN_POS);
    }

    /* rtc_seconds != 0 arms the PDS internal 32 kHz timer (pm_pds_mode_enter OR-sets the timer wake
     * bit when the count is non-zero). GPIO (above) and timer bits are independent -> wake on either. */
    const uint32_t cnt = rtc_seconds ? (32768u * rtc_seconds) : 0;
    pm_pds_mode_enter(PM_PDS_LEVEL_15, cnt);

    return -1;   /* not reached: PDS15 wake resets the chip */
}

#elif defined(BL616CL)

#include "bl616cl_pds.h"   /* per-pad PDS_Set_GPIO_Pad_* + PDS_WAKEUP_BY_* + PDS_GPIO_INT_* */
#include "bl616cl_hbn.h"   /* HBN_Pin_WakeUp_Mask */
#include "bl616cl_pm.h"    /* pm_pds_mask_all_wakeup_src / pm_pds_wakeup_src_en / pm_pds_mode_enter */

int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds)
{
    /* Same PDS15 reset-on-wake model as bl616; the PDS GPIO wake API is per-pad here instead of
     * bl616's per-group SET_x registers. (No AON_Output_Float_LDO15_RF on this chip.) */
    pm_pds_mask_all_wakeup_src();
    HBN_Pin_WakeUp_Mask(0xF);

    if (uart_wake) {
        /* Console RX = GPIO35 (bl616cl_board_config/board.c). Async falling-edge wake on the UART
         * start bit; the pad keeps its runtime UART pull-up. Validate the pull holds through PDS on
         * HW - if the line floats add a PDS-domain pull-up here. */
        const uint8_t rx_pad = 35;
        PDS_Set_GPIO_Pad_IntClr(rx_pad);
        PDS_Set_GPIO_Pad_IntMode(rx_pad, PDS_GPIO_INT_ASYNC_FALLING_EDGE);
        PDS_Set_GPIO_Pad_IntMask(rx_pad, UNMASK);
        pm_pds_wakeup_src_en(PDS_WAKEUP_BY_PDS_GPIO_INT);
    }

    const uint32_t cnt = rtc_seconds ? (32768u * rtc_seconds) : 0;
    pm_pds_mode_enter(PM_PDS_LEVEL_15, cnt);

    return -1;   /* not reached: PDS15 wake resets the chip */
}

#elif defined(BL602)

#include "bl602_pds.h"
#include "bl602_hbn.h"
#include "bl602_pm.h"    /* PDS_Mode_Enter(PDS_APP_CFG_Type *) */

int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds)
{
    /* PDS level 3: bl602_pm.c:570 resets the chip on wake for pdsLevel>1 (levels 0/1 resume in place),
     * so 3 gives the reboot-on-wake deep sleep matching bl616. powerDownFlash=0 keeps flash alive
     * (no flashCfg / flash re-init needed) at a small current cost. Wake sources: the AON pad GPIO7
     * (bl602's console RX GPIO3 is NOT wake-capable; wire the UART-RX/wake signal to GPIO7) and/or the
     * PDS 32 kHz timer (sleepTime). NOTE: no bl602 PDS reference existed in-tree — validate on HW. */
    PDS_APP_CFG_Type cfg = {
        .pdsLevel            = 3,
        .turnOffRF           = 1,
        .useXtal32k          = 0,   /* RC32K clocks the PDS wake timer */
        .pdsAonGpioWakeupSrc = uart_wake ? PDS_AON_WAKEUP_GPIO_7 : PDS_AON_WAKEUP_GPIO_NONE,
        .pdsAonGpioTrigType  = PDS_AON_GPIO_INT_TRIGGER_ASYNC_FALLING_EDGE,
        .powerDownFlash      = 0,
        .turnOffFlashPad     = 0,
        .ocramRetetion       = 1,
        .turnoffPLL          = 1,
        .xtalType            = PDS_PLL_XTAL_40M,
        .flashContRead       = 0,
        .sleepTime           = rtc_seconds ? (32768u * rtc_seconds) : 0,
        .flashCfg            = NULL,
        .ldoLevel            = PDS_LDO_LEVEL_1P00V,
        .preCbFun            = NULL,
        .postCbFun           = NULL,
    };
    PDS_Mode_Enter(&cfg);

    return -1;   /* not reached at pdsLevel 3: wake resets the chip */
}

#elif defined(BL618DG)

#include <stdio.h>

/* NOT PORTED YET. bl618dg is dual-core (AP + NP): a PDS entry has to agree with
 * the other core, so it is not a copy of the bl616/bl616cl single-core sequence
 * and must be brought up + verified on HW before it can be trusted. Fail loudly
 * at runtime rather than silently doing nothing (or worse, half-sleeping). */
int board_dsleep_enter(bool uart_wake, uint32_t rtc_seconds)
{
    (void)uart_wake;
    (void)rtc_seconds;
    printf("dsleep: not supported on bl618dg yet (dual-core PDS entry unported)\r\n");
    return -1;
}

#else
#error "board_dsleep_bouffalo.c: unexpected Bouffalo chip (expected BL602/BL616/BL616CL/BL618DG)"
#endif
