/*
 * hci_nostack.c — bring up the BT controller and run HCI WITHOUT linking btgap.a.
 *
 * Proof-of-concept for replacing Realtek's upper stack (btgap.a) with NimBLE.
 * The controller bring-up — patch download + HCI reset, the whole
 * hci_process_table sequence — lives entirely in our vendored transport
 * (hci_adapter.c / hci_process.c / hci_board.c / hci_uart.c). btgap.a's bte_init()
 * merely calls hci_tp_open() and then starts the host stack. Here we call
 * hci_tp_open() directly with our own open_cb + rx_ind, so NO btgap.a symbol is
 * needed — except trace_log_buffer, a generic utility symbol Realtek happened to
 * compile into btgap.a (the HCI_PRINT_* trace macros funnel into it); we
 * re-provide it as a no-op below. The old os_delay / os_mem_* utility symbols are
 * gone — their callers use vTaskDelay / pvPortMalloc / vPortFree directly now.
 *
 * This is the only bring-up path now that btgap.a is removed. After it completes,
 * bt_dtm_enter_bridge() exposes raw HCI on COM29.
 */
#include <string.h>
#include <stdarg.h>
#include "platform_stdlib_rtl8721d.h"   /* DiagPrintf */
#include "FreeRTOS.h"          /* vTaskDelay / pdMS_TO_TICKS (transport poll wait below) */
#include "task.h"
#include "hci_tp.h"            /* hci_tp_open, P_HCI_TP_OPEN_CB / RX_IND */
#include "hci_uart.h"          /* hci_uart_recv */

/* trace macros (HCI_PRINT_*) funnel here; we route console output through
 * hci_board_debug/DiagPrintf instead, so this is a no-op. */
void trace_log_buffer(uint32_t info, uint32_t log_str_index, uint8_t param_num, ...)
{
    (void)info; (void)log_str_index; (void)param_num;
}

/* ---- drive the controller init directly (replaces bte_init) ---- */
extern void     hci_tp_config(uint8_t *p_buf, uint16_t len);
extern uint16_t hci_uart_recv(uint8_t *p_buf, uint16_t size);

static volatile int s_bringup_done = 0;   /* 0 = pending, 1 = ok, -1 = fail */

static bool nostack_open_cb(bool status)
{
    s_bringup_done = status ? 1 : -1;
    return true;
}

/* rx_ind runs in the UART1 RX IRQ. We do NOT process there — the init sequence
 * (patch download etc.) is too heavy for the small handler stack (caused an
 * INVSTATE hard fault). receive_chars() already fills the rx ring regardless, so
 * this is a no-op and the work happens in the task-context poll loop below. */
static bool nostack_rx_ind_noop(void)
{
    return true;
}

/* Drain the rx ring and feed whole H4 HCI event packets
 * (0x04 <evt_code> <param_len> <params...>) to hci_tp_config(), which advances
 * the patch/reset state machine. Runs in the caller's TASK context. */
static void nostack_drain(void)
{
    static uint8_t  ev[300];
    static uint16_t have = 0, need = 3;
    static int      hdr_done = 0;
    uint8_t  tmp[64];
    uint16_t n;

    while ((n = hci_uart_recv(tmp, sizeof(tmp))) > 0) {
        for (uint16_t i = 0; i < n; i++) {
            if (have < sizeof(ev))
                ev[have] = tmp[i];
            have++;
            if (!hdr_done && have == 3) {       /* type + code + param_len read */
                need = 3 + ev[2];
                hdr_done = 1;
            }
            if (hdr_done && have == need) {      /* a complete event */
                hci_tp_config(ev, have);
                have = 0; need = 3; hdr_done = 0;
            }
        }
    }
}

/* Bring up the controller via the transport only (no bte_init / btgap.a).
 * Must run in a task with a real stack (called from wifi_bringup_thread, 16 KB).
 * Returns 1 on success, -1 on failure. */
int bt_hci_bringup_nostack(void)
{
    int spins = 0;

    s_bringup_done = 0;
    DiagPrintf("\n\r[BT] HCI bring-up WITHOUT btgap.a (hci_tp_open direct) ...\n\r");

    /* hci_tp_open() powers/resets the controller and sends init step 0. The IRQ
     * just buffers responses (no-op rx_ind); we drive the state machine here. */
    hci_tp_open(nostack_open_cb, nostack_rx_ind_noop);

    while (s_bringup_done == 0 && spins < 10000) {
        nostack_drain();
        vTaskDelay(pdMS_TO_TICKS(1));
        spins++;
    }

    if (s_bringup_done == 1)
        DiagPrintf("[BT] HCI bring-up OK (no btgap.a) - controller up, patch + reset via our transport\n\r");
    else
        DiagPrintf("[BT] HCI bring-up FAILED (no btgap.a), done=%d\n\r", s_bringup_done);
    return s_bringup_done;
}
