/*
 * BL618DG NP-core image.
 *
 * The NP core plays the role AmebaD's KM0 does: a minimal always-on image,
 * while the AP core carries the application (and, later, the WiFi stack).
 * Keep it deliberately small — it is appended into the AP image by
 * multi_bins.py, so every byte here costs AP flash budget.
 *
 * Console: the SDK's own dualcore example puts the NP shell on uart1; the AP
 * core owns uart0 (GPIO11, the board's wired console).
 */
#include <stdio.h>

#include "bflb_mtimer.h"
#include "board.h"

int main(void)
{
    board_init();

    printf("[NP] bl618dg np core alive\r\n");

    while (1) {
        bflb_mtimer_delay_ms(5000);
        printf("[NP] tick\r\n");
    }
}
