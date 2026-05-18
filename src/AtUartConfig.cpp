#include "AtUartConfig.h"

#if defined AT_UART_BACKEND_BFLB
#  include <bflb_gpio.h>
#  include <bflb_uart.h>
#elif defined AT_UART_BACKEND_AMEBA
#  include "UartAmebaGlue.h"
#endif

namespace at {

namespace {

constexpr uint32_t kBaud = 115200;

#if defined AT_UART_BACKEND_BFLB
// AT 串口 per doc/serial_config.md (UART1 on all Bouffalo chips).
#  if defined BL602 || defined BL702
constexpr uint8_t kTxPin = 16;
constexpr uint8_t kRxPin = 7;
#  elif defined BL616
constexpr uint8_t kTxPin = 21;
constexpr uint8_t kRxPin = 22;
#  elif defined BL616CL
constexpr uint8_t kTxPin = 34;
constexpr uint8_t kRxPin = 35;
#  else
constexpr uint8_t kTxPin = 16;
constexpr uint8_t kRxPin = 7;
#  endif
#elif defined AT_UART_BACKEND_AMEBA
// AT 串口 per doc/serial_config.md. Pin encoding: PORT_X<<5 | N (PA=0, PB=1).
// SDK's serial_init routes to the right UART instance based on pin.
#  if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C
// Ameba D: UART0, PB_1 / PB_2
constexpr int kTxPin = (1 << 5) | 1;
constexpr int kRxPin = (1 << 5) | 2;
#  elif defined CONFIG_AMEBADPLUS
// Ameba D+: UART1, PB_31 / PB_30
constexpr int kTxPin = (1 << 5) | 31;
constexpr int kRxPin = (1 << 5) | 30;
#  elif defined CONFIG_AMEBAGREEN2
// Ameba Green 2: UART1, PA_26 / PA_25
constexpr int kTxPin = (0 << 5) | 26;
constexpr int kRxPin = (0 << 5) | 25;
#  else
constexpr int kTxPin = (0 << 5) | 7;
constexpr int kRxPin = (0 << 5) | 8;
#  endif
#endif

}  // namespace

UartDevice *openAtUart() {
#if defined AT_UART_BACKEND_BFLB
    auto *gpio = bflb_device_get_by_name("gpio");
    bflb_gpio_uart_init(gpio, kTxPin, GPIO_UART_FUNC_UART1_TX);
    bflb_gpio_uart_init(gpio, kRxPin, GPIO_UART_FUNC_UART1_RX);

    auto *uart = bflb_device_get_by_name("uart1");
    struct bflb_uart_config_s cfg = {};
    cfg.baudrate  = kBaud;
    cfg.data_bits = UART_DATA_BITS_8;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.parity    = UART_PARITY_NONE;
    bflb_uart_init(uart, &cfg);
    return uart;
#elif defined AT_UART_BACKEND_AMEBA
    return at_uart_create(kTxPin, kRxPin, kBaud);
#else
    return nullptr;
#endif
}

}  // namespace at
