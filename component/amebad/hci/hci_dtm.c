/*
 * hci_dtm.c — BLE DTM (Direct Test Mode) bring-up, replicating the SDK's
 * ATM2=bt_power,on / gnt_bt,bt / bridge command path
 * (platform/.../component/common/api/at_cmd/atcmd_mp_ext2.c). KM4 only.
 *
 *   bt_dtm_bringup()      = ATM2=gnt_bt,bt + ATM2=bt_power,on:
 *                           rtlk_bt_set_gnt_bt_with_clk_source(PTA_BT) (grant the
 *                           shared RF to BT) + bt_trace_init() + bte_init() —
 *                           powers the BT controller, downloads the firmware
 *                           patch over HCI-UART (UART1), completes HCI Reset.
 *                           (bte_init is in btgap.a; the coex grant + patch
 *                           getter are in lib_wlan.a.)
 *   bt_dtm_enter_bridge() = ATM2=bridge: hijack the LOGUART (console / COM29) as
 *                           a transparent HCI pipe so a host BT-test tool drives
 *                           DTM. NB: this TAKES OVER the console until reset.
 *
 * Prereq: WiFi must be ON (hci_board_init() checks wifi_is_up) — call after
 * wifi_on(STA). DTM needs WiFi on, not connected.
 */
#include "platform_stdlib_rtl8721d.h"  /* DiagPrintf, uint*_t */
#include <gpio_api.h>         /* PinName, _PA_7 / _PA_8 */
#include "ameba_soc.h"        /* UART2_DEV, UART_Readable/CharGet/ClearRxFifo, UART_LOG_IRQ, InterruptDis */
#include "bt_intf.h"          /* rtlk_bt_set_gnt_bt_with_clk_source, PTA_BT */

/* vendored transport. */
extern int  bt_hci_bringup_nostack(void);   /* hci_nostack.c: hci_tp_open + util syms */

/* HCI controller UART is UART1 on RTL8721D (hci_uart.c: HCI_UART_IDX==1 ->
 * HCI_UART_DEV = UART1_DEV); the host console LOGUART is UART2_DEV. */
#define BT_HCI_UART_DEV  UART1_DEV   /* controller side */
#define BT_HOST_UART_DEV UART2_DEV   /* host / COM29 side (LOGUART) */

/* ATM2=gnt_bt,bt  +  ATM2=bt_power,on — controller bring-up via our HCI transport
 * only (no Realtek upper stack / btgap.a). Grant the shared RF to BT, start trace,
 * then download the firmware patch + HCI reset directly through hci_tp_open()
 * (hci_nostack.c). MP vs normal (AMEBAD_HCI_FW_MP) only selects the patch blob.
 *
 * NB: WiFi/BT coex glue (bt_coex_init / rtk_coex.c) was removed with btgap.a; the
 * coex algorithm still lives in lib_wlan.a + the controller firmware. A future host
 * stack (NimBLE) re-wires the BT-state -> WiFi mailbox if concurrent WiFi+BLE is
 * needed. For DTM / HCI testing the raw antenna grant below is enough. */
void bt_dtm_bringup(void)
{
#if defined(CONFIG_MP_INCLUDED) && CONFIG_MP_INCLUDED
    DiagPrintf("\n\r[BT] bring-up (MP / RF-test fw): gnt_bt(BT) + hci_tp_open ...\n\r");
#else
    DiagPrintf("\n\r[BT] bring-up (normal BLE fw): gnt_bt(BT) + hci_tp_open ...\n\r");
#endif
    rtlk_bt_set_gnt_bt_with_clk_source(PTA_BT);
    bt_hci_bringup_nostack();
}

/* ATM2=bridge — turn the LOGUART (console / COM29) into a transparent HCI pipe so
 * a host BT-test tool drives DTM. This NEVER returns (the LOGUART is a raw HCI pipe
 * until reset).
 *
 * Why a full polling bridge instead of the SDK's bt_uart_bridge_open(): on this
 * driver-only KM4 image neither IRQ path the SDK relies on delivers —
 *   - host->controller: the SDK relay is driven by the LOGUART RX *interrupt*
 *     (registered on UART_LOG_IRQ_LP), but the shell owns UART_LOG_IRQ and
 *     silently buffers the (non-'\r') HCI bytes, so the relay handler never ran
 *     (verified: zero host bytes reached the bridge);
 *   - controller->host: routing controller RX through the transport IRQ +
 *     set_hci_uart_out() bridge_flag never reached hci_uart_rx_bridge either
 *     (verified: zero reply bytes).
 * So we bypass both IRQs entirely: take the host LOGUART RX away from the shell
 * (InterruptDis) and the controller HCI-UART (UART1) RX away from the BT transport
 * (disable its RX interrupt), then shuttle raw bytes between the two UARTs in a
 * tight poll loop. Deterministic, no IRQ-ownership / bridge_flag assumptions.
 *
 * host_baud is accepted for signature compatibility; the console is already at the
 * host rate (1.5 Mbps) so we don't re-rate the LOGUART. This never returns. */
void bt_dtm_enter_bridge(uint32_t host_baud, uint8_t parity)
{
    (void)parity;
    /* Marker the host (project/amebad/hci_test.ps1) syncs on — printed BEFORE we
     * hijack the UART, while DiagPrintf/TX still interleaves normally. */
    DiagPrintf("[BT] entering HCI bridge (polling, host_baud=%d) — console is now a raw HCI pipe\n\r",
               (int)host_baud);

    /* Stop the two IRQ owners from draining the RX FIFOs we are about to poll. */
    InterruptDis((IRQn_Type)UART_LOG_IRQ);                                  /* shell off host UART */
    UART_INTConfig(BT_HCI_UART_DEV, RUART_IER_ERBI | RUART_IER_ETOI, DISABLE); /* transport off ctrl UART */
    UART_ClearRxFifo(BT_HOST_UART_DEV);
    UART_ClearRxFifo(BT_HCI_UART_DEV);

    for (;;) {
        u8 b;
        if (UART_Readable(BT_HOST_UART_DEV)) {   /* host -> controller */
            UART_CharGet(BT_HOST_UART_DEV, &b);
            while (!UART_Writable(BT_HCI_UART_DEV));
            UART_CharPut(BT_HCI_UART_DEV, b);
        }
        if (UART_Readable(BT_HCI_UART_DEV)) {    /* controller -> host */
            UART_CharGet(BT_HCI_UART_DEV, &b);
            while (!UART_Writable(BT_HOST_UART_DEV));
            UART_CharPut(BT_HOST_UART_DEV, b);
        }
    }
}
