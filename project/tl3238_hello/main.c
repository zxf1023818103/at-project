/* SPDX-License-Identifier: Apache-2.0 */
#include "common.h"

#define HELLO_UART UART0
#define HELLO_BAUD 115200u

int main(void)
{
    unsigned short divider = 0;
    unsigned char bit_width = 0;
    static const char message[] = "hello world\r\n";

    PLATFORM_INIT;
    CLOCK_INIT;

    uart_clk_en(HELLO_UART);
    uart_hw_fsm_reset(HELLO_UART);
    uart_set_pin(HELLO_UART, GPIO_FC_PA1, GPIO_FC_PA2);
    uart_cal_div_and_bwpc(HELLO_BAUD, sys_clk.pclk * 1000000u,
                         &divider, &bit_width);
    uart_init(HELLO_UART, divider, bit_width,
              UART_PARITY_NONE, UART_STOP_BIT_ONE);
    /* The FSM reset preserves configuration left by the boot ROM. */
    uart_set_cts_dis(HELLO_UART);
    uart_set_rts_dis(HELLO_UART);

    while (1) {
        for (unsigned int i = 0; i < sizeof(message) - 1u; ++i) {
            (void)uart_send_byte(HELLO_UART, (unsigned char)message[i]);
        }
        delay_ms(1000);
    }
}
