# BL618DG pin mux and conflicts

Which peripheral can live on which pad, and what collides with what. Written for
board bring-up and for re-assigning pins away from the bl618dgdk defaults.

Sources: *BL618DG Datasheet v0.9.4* (2026-04-21) Table 3.1 "Pin definition",
§2.7 "Boot" and §2.9, cross-checked against `platform/bouffalo_sdk`
(`drivers/lhal/include/bflb_gpio.h`, `bsp/board/bl618dgdk/board_gpio.c`,
`drivers/lhal/config/bl618dg/`, `drivers/soc/bl618dg/std/src/bl618dg_glb.c`).
Ethernet has its own document: `doc/bl618dg_emac.md`.

## Packages

49 GPIOs, `GPIO0`..`GPIO48`. QFN68 brings out all of them; **QFN56 omits 12**:

> GPIO8, 9, 10, 19, 20, 21, 22, 23, 24, 25, 26, 42

Everything else keeps the same GPIO number on both packages, only the physical
pin number differs. Non-GPIO pins: `ANT_5G`, `ANT_2G`, `ANT_BT`, `AVDD_RF2/RF3`,
`AVDD33`, `AVDD33_RF3`, `VDDCORE`, `VDD18`, **`VDD_SDIO`** (QFN56 pin 34 /
QFN68 pin 43 -- the SD/SDIO pad group has its own supply rail), `DCDC_OUT`,
`SW_DCDC`, `PU_CHIP`.

## Boot straps (§2.7)

| Pin | Level | Behaviour |
|---|---|---|
| **GPIO33** | 1 | Boot from UART (GPIO11/12 **or** GPIO11/42) / USB (GPIO40/41) / SDIO -- flashing & RAM download |
| GPIO33 | 0 | Boot from flash, run the application image |

Consequences for board design:

- **GPIO33 is the BOOT pin** (this is the line the bench wiring drives from
  COM31 DTR, active-low through the adapter). It is also `E0_RX_ERR`,
  `CANFD1_TX`, `CAM0_DATA7` and `UART_SIG9` -- anything wired there must not
  fight the strap at reset.
- ISP over UART uses **GPIO11/12** (or GPIO11/42). GPIO11 is also the AP console
  TX, so console and ISP share the pad by design.
- ISP over USB uses **GPIO40/41** = `USB_DP`/`USB_DM` (function 10).

## Function numbers

Set via the `GPIO_FUNC_*` field of `bflb_gpio_init()` (`bflb_gpio.h`, BL618DG arm):

| # | Macro | Signals |
|---|---|---|
| 0 | `GPIO_FUNC_SDH` | SD **host**: CLK/CMD/Data0-3 |
| 1 | `GPIO_FUNC_SPI` (`SPI0`/`SPI1`) | SPI0-3: SS/SCLK/MISO/MOSI |
| 2 | – | PDM (`pdm_clk_o`/`pdm_0_in`) on most pads, **serial flash** (`SFlash_*`) on two groups |
| 3 | `GPIO_FUNC_I2S` | I2S BCLK/FS/DI/DO |
| 4 | `GPIO_FUNC_CANFD` | CANFD0/1/2 RX+TX |
| 5 | `GPIO_FUNC_I2C0` / `I2C1` | I2C0 & I2C1 SDA/SCL |
| 6 | `GPIO_FUNC_I2C2` | I2C2 SDA/SCL |
| 7 | `GPIO_FUNC_UART` | `UART_SIG0..11` (see UART below) |
| 8 | `GPIO_FUNC_EMAC` | RMII for EMAC0/EMAC1 |
| 9 | `GPIO_FUNC_CAM` | CAM0 data/clk/sync |
| 10 | `GPIO_FUNC_ANALOG` | ADC channels, touch channels, AUADC, vref, **USB_DP/DM** |
| 11 | `GPIO_FUNC_GPIO` | software GPIO (`SWGPIO[n]`), or `irrx` when `ir_rx_gpio_sel=1` |
| 12 | `GPIO_FUNC_SDU` / `IRTX` | SD **device** (SDIO slave) on the SD pads, `irtx` on GPIO0-7 |
| 16 | `GPIO_FUNC_PWM` | PWM0/PWM1 channels, `reg_pwm1_io_sel` picks p/n |
| 17 | `GPIO_FUNC_JTAG_M1` / `JTAG_LP` | WL/EP JTAG |
| 18 | `GPIO_FUNC_UART3` | UART3 RTS/CTS/TXD/RXD (a separate fixed-map UART) |
| 21 | `GPIO_FUNC_AUDAC` | `AUDAC_PWM_P/N` |
| 22 / 23 | `GPIO_FUNC_DBI_B` / `DBI_C`(`DBI_QSPI`) | display bus interface |
| 24 | `GPIO_FUNC_DPI` | parallel display |
| 26 | `GPIO_FUNC_JTAG_M0` | main core JTAG (`main_jtag_*`) |
| 27 | `GPIO_FUNC_PEC` | `PEC_<n>`, one channel per pad |
| 28-31 | – | `rf_epa_gpio[]`, `ant_switch_cntl[]`, `epta_wl_act`, `chip_clk_out[0..3]` |

## The mux is periodic

Most functions are **not** tied to specific pads -- they repeat with a fixed
period across the whole GPIO range, so a peripheral can usually be moved out of
the way:

| Function | Period | Rule |
|---|---|---|
| CANFD (4) | 6 | `CANFD[(n mod 6) / 2]_{RX if n even else TX}` |
| I2S (3) | 4 | BCLK / FS / DI / DO |
| I2C0-1 (5) | 4 | I2C0_SDA / I2C0_SCL / I2C1_SDA / I2C1_SCL |
| I2C2 (6) | 2 | SCL on even, SDA on odd |
| UART (7) | 12 | `UART_SIG(n mod 12)` |
| UART3 (18) | 4 | RTS / CTS / TXD / RXD |
| SPI (1) | 16 | `SPI[(n mod 16)/4]_{SS,SCLK,MISO,MOSI}` |
| PDM (2) | 2 | `pdm_clk_o` on even, `pdm_0_in` on odd (where not serial flash) |

The exceptions -- functions that exist only on a fixed set of pads -- are EMAC,
serial flash, SDH/SDIO, CAM, USB and the analog channels. Those are the ones
worth planning a board around.

## Ethernet (function 8)

Two RMII MACs; only these 20 pads carry EMAC signals. Full detail, board defaults
and driver notes in `doc/bl618dg_emac.md`.

- **EMAC0**: GPIO16 `TX_EN`, 17 `MDIO`, 18 `MDC`, 27 `REF_CLK`, 28 `TXD[0]`,
  29 `TXD[1]`, 30 `RXD[0]`, 31 `RXD[1]`, 32 `RX_DV`, 33 `RX_ERR` (optional)
- **EMAC1**: GPIO0 `REF_CLK`, 1 `TXD[0]`, 2 `TXD[1]`, 3 `RXD[0]`, 4 `RXD[1]`,
  5 `RX_ERR` (optional), 6 `TX_EN`, 7 `RX_DV`, 13 `MDIO`, 14 `MDC`

EMAC is 10/100 RMII only. Pads are shared with the (undriven) v1 MAC IP; the
board layer selects GMAC via `0x20000390` bit30 (EMAC0) / bit31 (EMAC1).
Note `E0_RX_ERR` sits on the BOOT strap (GPIO33) -- another reason it is left
unconnected by default.

## CAN FD (function 4)

Three controllers: `CAN0` @`0x2000E000`, `CAN1` @`0x2000E800`, `CAN2` @`0x2000F000`,
each with its own IRQ (`irq_num = 0xff` on the LP core). Driver `bflb_canfd.c` is
in the bl618dg build; there is **no example project** in the SDK.

Every GPIO0-48 can carry CAN FD, following the period-6 rule above -- no
exceptions:

| Controller | RX pads | TX pads |
|---|---|---|
| CANFD0 | 0, 6, 12, 18, 24, 30, 36, 42, 48 | 1, 7, 13, 19, 25, 31, 37, 43 |
| CANFD1 | 2, 8, 14, 20, 26, 32, 38, 44 | 3, 9, 15, 21, 27, 33, 39, 45 |
| CANFD2 | 4, 10, 16, 22, 28, 34, 40, 46 | 5, 11, 17, 23, 29, 35, 41, 47 |

Board defaults (`board_canfd_gpio_init(idx)`), **all three collide with EMAC1**:

| idx | RX | TX | collides with |
|---|---|---|---|
| 0 | GPIO6 | GPIO7 | `E1_TX_EN`, `E1_RX_DV` |
| 1 | GPIO14 | GPIO15 | `E1_MDC` (GPIO15 is free) |
| 2 | GPIO4 | GPIO5 | `E1_RXD[1]`, `E1_RX_ERR` |

## SD host / SDIO device (functions 0 and 12)

Two independent controllers, both SDIO 3.0:

| Block | Role | Base | IRQ | Device name | Driver |
|---|---|---|---|---|---|
| **SDH** | SDIO 3.0 **host** (drives SD cards / SDIO cards) | `0x20084000` | `BL618DG_IRQ_SDH` | `BFLB_NAME_SDH` | `bflb_sdh.c` |
| **SDU** | SDIO 3.0 **device** (chip acts as an SDIO card to an external host) | `0x20085000` | `BL618DG_IRQ_SDIO` | `BFLB_NAME_SDIO3` | `bflb_sdio3.c` |

Datasheet capabilities -- host (§2.9.4): SD Host Controller Spec 3.0, SD physical
layer 3.01, SDIO spec 3.0, DS/HS plus UHS-I SDR12/SDR25/SDR50/SDR104, clock
tuning, ADMA2. Device (§2.9.5): SPI / 1-bit / 4-bit modes, clock up to 208 MHz,
embedded DMA, command/transfer interrupts, timing tuning; the external host can
reach the chip's internal SDIO registers and RAM.

**Both controllers are muxed onto the same two 6-pad groups**, host as function 0
and device as function 12:

| Group | CLK | CMD | Data0 | Data1 | Data2 | Data3 |
|---|---|---|---|---|---|---|
| A | GPIO37 | GPIO36 | GPIO38 | GPIO39 | GPIO34 | GPIO35 |
| B | GPIO46 | GPIO45 | GPIO47 | GPIO48 | GPIO43 | GPIO44 |

Board defaults, both on **group B**:

```c
board_sdh_gpio_init()   -> GPIO43..48 with GPIO_FUNC_SDH   /* function 0  */
board_sdio_gpio_init()  -> GPIO43..48 with GPIO_FUNC_SDU   /* function 12 */
```

Note `board_sdio_gpio_init()` sets GPIO46 (CLK) to `GPIO_PULLDOWN | GPIO_DRV_0`
while the other five are `GPIO_PULLUP | GPIO_DRV_1` -- SD bus convention (clock
must not float high, data/cmd idle high).

Practical rules:

- Host and device **cannot share a group** -- one pad, one function. Running both
  at once means SDH on group A and SDU on group B (or vice versa), which consumes
  all 12 pads.
- Group A and group B are also the two **serial-flash** groups (see below), so an
  external-flash board only has one SD group left.
- SDIO is a boot source (§2.7); the ROM's SDIO ISP path uses these pads too.
- The group has a dedicated supply pin, `VDD_SDIO` -- level-shift accordingly if
  the SD side is not at the same rail as the rest of the I/O.

## Serial flash (function 2)

Serial-flash signals exist on **the same two groups**:

| Group | CS | CLK | D0 | D1 | D2 | D3 |
|---|---|---|---|---|---|---|
| A | GPIO34 | GPIO39 | GPIO35 | GPIO36 | GPIO37 | GPIO38 |
| B | GPIO43 | GPIO48 | GPIO44 | GPIO45 | GPIO46 | GPIO47 |

So **external flash, SD host and SDIO device all compete for the same 12 pads.**

**Embedded flash does not.** `GLB_Set_SFlash_IO_PARM(selEmbedded=1, ...)` sets
`GLB_SEL_EMBEDDED_SFLASH`, and `GLB_Embedded_Flash_Pad_Enable()` drives pads
**53-58** -- internal-only pads above the 0-48 external range. With embedded
flash (the bl618dgdk case) GPIO34-39 and GPIO43-48 stay free for other uses.
`swapIo3Io0` / `swapIo2Cs` only reshuffle those internal pads.

## USB (function 10)

`USB_DP` = **GPIO40**, `USB_DM` = **GPIO41** -- fixed, no alternatives. USB 2.0
HS with integrated PHY, host/device capable, controller @`0x20087000`. These two
pads are also the USB ISP path when GPIO33 is strapped high. Anything else placed
on GPIO40/41 (they are `CANFD2_RX/TX`, `SPI2_SS/SCLK`, `UART_SIG4/5`, `I2C0`,
`chip_clk_out[0/1]`) rules USB out.

## CAM, analog, and the rest

- **CAM0** (function 9) lives on GPIO0-10, 15-18, 27-33 only:
  `DATA0-7`, `HSYNC`, `VSYNC`, `CLK`.
- **Analog** (function 10): ADC0/ADC1 channels, `touch_ch[]`, `AUADC`, `vref_ext`
  over GPIO0-18 and 27-33, plus USB_DP/DM on GPIO40/41.
- **JTAG**: function 17 = WL/EP JTAG, function 26 = main-core JTAG
  (`main_jtag_tms/tck/tdi/tdo`) -- the latter repeats every 4 pads, so leave a
  set reachable if you want to debug.
- **PEC** (function 27): `PEC_<n>` where n == the GPIO number, one per pad.

## Conflict map

| Resource | Pads | Notes |
|---|---|---|
| **BOOT strap** | GPIO33 | also `E0_RX_ERR`, `CANFD1_TX`, `CAM0_DATA7`, `UART_SIG9` |
| AP console (UART0 TX) | GPIO11 | also UART-ISP pin; `CANFD2_TX`, `SPI2_MOSI`, `I2C1_SCL` |
| UART ISP | GPIO11 + GPIO12 (or GPIO42) | GPIO42 is QFN68-only |
| USB / USB ISP | GPIO40, 41 | fixed `USB_DP`/`USB_DM` |
| EMAC0 | 16-18, 27-32 (+33) | overlaps CAM0 (27-33), analog, CANFD, SPI, **BOOT** |
| EMAC1 | 0-7, 13, 14 (+5) | overlaps CAM0 (0-10), `irtx` (0-7), **all three default CAN FD pairs** |
| Ext. flash / SDH / SDIO device | 34-39 (A), 43-48 (B) | one group each; free if flash is embedded |
| CAM0 | 0-10, 15-18, 27-33 | conflicts with **both** EMAC groups |
| CAN FD | period 6, whole range | easy to relocate |

Practical consequences:

1. **Dual Ethernet + camera is impossible** -- CAM0's pads are a subset of the
   two EMAC groups.
2. **External flash + SD host + SDIO device cannot coexist** -- three consumers,
   two groups. Embedded flash frees one.
3. With EMAC0 and EMAC1 both up, the remaining pads are GPIO8-12, 15, 19-26,
   34-48; on QFN56 that shrinks to GPIO11, 12, 15, 34-41, 43-48 -- and GPIO11
   (console), 33 (BOOT), 40/41 (USB) are spoken for.

## Two pad groups, three consumers

The SD/flash pads are **two independent groups**, so external flash and SDH are
not inherently exclusive -- each can take one group. The SDK names the external
flash options `sf2` and `sf3` (`drivers/lhal/src/flash/bflb_sf_cfg.h`, BL618DG
arm), and they map exactly onto the groups derived from the datasheet:

```c
sf2: CLK=GPIO39  CS=GPIO34  D0-3=GPIO35,36,37,38      /* group A */
sf3: CLK=GPIO48  CS=GPIO43  D0-3=GPIO44,45,46,47      /* group B */
```

⚠️ The comments in `bflb_sf_ctrl.h` ("SF2 use gpio4-9", "SF3 use gpio10-15") are
stale text carried over from BL602/BL616 -- for BL618DG the `BFLB_EXTFLASH_*_GPIO`
macros above are authoritative.

Embedded and external flash can also coexist: `SF_IO_EMB_*_AND_SF2*`
(0x14-0x37) selects embedded flash **plus** external SF2, so a second storage
chip is possible without giving up the internal boot flash. Boot media selection
lives in the boot header's `flash_pin_cfg`; the ROM/SDK default is
`sel_embedded = 1` (`bflb_sf_cfg_init_flash_gpio()`).

Feasibility with both EMACs up (18 pads), console on GPIO11 and BOOT on GPIO33:

| Combination | Group A (34-39) | Group B (43-48) | QFN56 | QFN68 |
|---|---|---|---|---|
| embedded flash + SDH + 3x CAN FD | CAN FD x3 | SDH | yes | yes |
| external flash + SDH, no CAN FD here | flash (sf2) | SDH | yes | yes |
| external flash + SDH + 3x CAN FD | flash | SDH | **no** (only 1 CAN FD fits) | yes (CAN FD on 20-25) |
| embedded + external flash + SDH | flash (sf2) | SDH | yes | yes |
| flash + SDH + SDIO device | three consumers, two groups | | no | no |

The QFN56 "no" row: after EMAC x2 + flash + SDH + console + BOOT, the free pads
are GPIO5, 12, 15, 40, 41. CANFD0 needs a TX from {1,7,13,19,25,31,37,43} and
CANFD1 an RX from {2,8,14,20,26,32,38,44} -- neither intersects, so only CANFD2
(RX 40 / TX 5 or 41) can be placed, and that eats USB. Dropping EMAC1 fixes it:
CANFD0 = GPIO0/1, CANFD1 = GPIO2/3, CANFD2 = GPIO4/5, USB intact.

## Recommended allocation

### A. Embedded flash, dual EMAC, 3x CAN FD, SDH -- fits QFN56 (32 of 37 GPIOs)

| Use | Pads |
|---|---|
| EMAC0 | 16, 17, 18, 27, 28, 29, 30, 31, 32 (`RX_ERR` 33 unconnected) |
| EMAC1 | 0, 1, 2, 3, 4, 6, 7, 13, 14 (`RX_ERR` 5 unconnected) |
| CANFD2 | 34 (RX) / 35 (TX) |
| CANFD0 | 36 (RX) / 37 (TX) |
| CANFD1 | 38 (RX) / 39 (TX) |
| SDH | 43 `D2`, 44 `D3`, 45 `CMD`, 46 `CLK`, 47 `D0`, 48 `D1` |
| Console / UART ISP | 11 (+12) |
| BOOT strap | 33 |

Left free: GPIO5, 12, 15, 40, 41 -- so **USB (40/41) still fits**, as does the
second UART-ISP pin (12).

### B. QFN68, also keeping external flash (38 of 49 GPIOs)

Move CAN FD into the QFN68-only block and both groups stay available:

| Use | Pads |
|---|---|
| CANFD1 | 20 (RX) / 21 (TX) |
| CANFD2 | 22 (RX) / 23 (TX) |
| CANFD0 | 24 (RX) / 25 (TX) |
| External flash (sf2) | 34-39 |
| SDH | 43-48 |

EMAC, console, BOOT and USB as in A. Left free: GPIO5, 8, 9, 10, 12, 15, 19, 26,
42 (plus 40/41 if USB is unused).

### C. External flash, dual EMAC, SDH, no CAN FD -- fits QFN56 (32 of 37 GPIOs)

| Use | Pads |
|---|---|
| EMAC0 | 16, 17, 18, 27, 28, 29, 30, 31, 32 |
| EMAC1 | 0, 1, 2, 3, 4, 6, 7, 13, 14 |
| External flash (sf2) | 34 `CS`, 35 `D0`, 36 `D1`, 37 `D2`, 38 `D3`, 39 `CLK` |
| SDH | 43 `D2`, 44 `D3`, 45 `CMD`, 46 `CLK`, 47 `D0`, 48 `D1` |
| Console / UART ISP | 11 (+12) |
| BOOT strap | 33 |

Every one of those pads exists on QFN56 (the missing ones are GPIO8-10, 19-26,
42). Left free: GPIO5, 12, 15, 40, 41 -- enough for USB (40/41) **or** one I2C0
(SDA=12, SCL=5) plus a spare, but not both. Flash and SDH may swap groups
(flash on sf3 / SDH on group A) with no other change.

**C+: add USB and the full UART-ISP pair** -- GPIO40/41 for `USB_DP`/`USB_DM`,
GPIO11+12 for console/ISP. That is 35 of 37 GPIOs, leaving GPIO5 and GPIO15.
Those two cannot form an I2C (both land on odd, i.e. same-role, positions in
every I2C mapping) nor a CAN FD pair (both are TX candidates), but through the
GLB UART mux they do make one extra full UART: GPIO5 = `UART_SIG5`,
GPIO15 = `UART_SIG3`, bound to any UARTx TX/RX via `bflb_gpio_uart_init()`.
GPIO15 also carries `ADC1_1`/`touch_ch[3]` if an analog input is wanted instead.

⚠️ In C+ there is **no room for a 4-wire JTAG** -- `main_jtag_*` (function 26)
repeats every 4 pads and GPIO5/15 only expose `tck` and `tdi`. Plan debug around
UART, or borrow SDH/flash pads during bring-up before those parts are populated.

Adding CAN FD to this configuration costs something: on QFN56 only CANFD2 can be
placed (RX=40, TX=5 or 41) and it takes USB's pads. Three CAN FD channels here
require dropping EMAC1 (then CANFD0=0/1, CANFD1=2/3, CANFD2=4/5) or moving to
QFN68 (option B).

### What cannot be added either way

- **SDIO device (SDU)** -- shares the same two groups with SDH and external flash.
- **CAM0** -- its pads are a subset of the two EMAC groups.

Note that pin availability is not bandwidth: SDH (UHS-I up to SDR104), two EMACs
and three CAN controllers all contend for the AXI/AHB buses and the 20 DMA
channels (8+8+4).

Changing a pin is just the pad number in `bflb_gpio_init()` -- CAN FD, EMAC, I2C,
SPI, I2S, SD and flash have no signal-select register behind the pad, the pad
*is* the choice. The exceptions with a select bit are IR RX (`ir_rx_gpio_sel`),
PWM1 (`reg_pwm1_io_sel`), UART (the GLB UART mux, `bflb_gpio_uart_init()`), the
SPI signal-swap groups (`GLB_SPI_Sig_Swap_Set`: GPIO0-11 / 12-23 / 24-35 /
36-45), and flash (`GLB_Set_SFlash_IO_PARM` / `bflb_sf_ctrl_select_pad`).

## UART (function 7 and 18)

Function 7 exposes 12 *signals*, not fixed TX/RX: `UART_SIG0..11`, repeating every
12 pads. Any signal can be bound to any of UART0/1/2's RTS/CTS/TX/RX through the
GLB UART mux -- in the SDK, `bflb_gpio_uart_init(gpio, pin, GPIO_UART_FUNC_UARTx_yy)`
(`GPIO_UART_FUNC_UART0_RTS`=0 .. `UART2_RX`=11). So UART placement is essentially
free, at the cost of one `UART_SIGn` slot per signal.

**The AP console is UART0 TX on GPIO11 @ 2000000 8N1** (= `UART_SIG11`).

Function 18 (`UART3`) is a separate, fixed-map UART: RTS/CTS/TXD/RXD repeating
every 4 pads across the whole range.

## Module: Ai-M61D-32S (Ai-Thinker, SMD-40)

A BL618DG module in the **QFN56** flavour, spec V1.0.0 / 2026-06-25. 40 pads:
GND, VDD, EN + **37 IOs** -- which is exactly the full QFN56 GPIO set, i.e. the
module brings out every pad the package has. (Section 1.1 of the spec says
"默认引出 40 个 GPIO"; Table 1 says "可用 IO 数量 默认 37 个" -- 37 is the correct
number, 40 is the pad count.)

| Module pin | IO | | Module pin | IO | | Module pin | IO | | Module pin | IO |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | GND | | 11 | IO13 | | 21 | IO39* | | 31 | IO0 |
| 2 | VDD | | 12 | IO29 | | 22 | IO43 | | 32 | IO1 |
| 3 | EN | | 13 | IO30 | | 23 | IO44 | | 33 | IO2 |
| 4 | IO28 | | 14 | IO31 | | 24 | IO40 | | 34 | IO6 |
| 5 | IO18 | | 15 | IO32 | | 25 | IO41 | | 35 | IO7 |
| 6 | IO17 | | 16 | IO34* | | 26 | IO45 | | 36 | IO12 |
| 7 | IO27 | | 17 | IO35* | | 27 | IO33/BOOT | | 37 | IO11 |
| 8 | IO16 | | 18 | IO36* | | 28 | IO46 | | 38 | IO5 |
| 9 | IO15 | | 19 | IO37* | | 29 | IO47 | | 39 | IO4 |
| 10 | IO14 | | 20 | IO38* | | 30 | IO48 | | 40 | IO3 |

`*` IO34-39 = the sf2 external-flash group. Availability depends on the module's
flash variant, and **the spec contradicts itself**: the table says "外置 Flash 版本
此 IO 与内部 Flash 共用未引出" while note 1 says "内置 Flash 版本 IO34~39 不可用".
The table is the one consistent with the silicon -- external flash *is* sf2
(GPIO34-39), embedded flash uses internal pads 53-58 and costs no external IO --
so the external-flash variant is the one that loses IO34-39.

**Confirmed by the sibling module's schematic** (Ai-M65P-32S V1.0, 2026-07-28,
drawing 21013227): the flash chip U4 is wired straight to the SoC on nets named
`GPIO34/SF2_CS`, `GPIO35/SF2_D0`, `GPIO36/SF2_D1`, `GPIO37/SF2_D2`,
`GPIO38/SF2_D3`, `GPIO39/SF2_CLK` (22R series resistor R1 on SCLK, R9-R14
not-mounted). So on any external-flash module those six IOs are gone. Note 1 of
the M61D spec is simply a typo -- but still worth confirming in writing.

The Ai-M65P-32S shares the same SMD-40 pinout. Its schematic labels U1 with three
candidate part numbers (`BL616D_QFN56`, `BL618D_QFN56`, `BL618_QFN56`), but the
pin assignment matches the **BL618DG QFN56** datasheet exactly -- GPIO11-18 on
pins 5-10/13/14, `VDD_SDIO` on pin 34, GPIO40/41 (USB) on pins 29/30, GPIO43-48
on pins 31-37, and the AVDD33/PU_CHIP/VDD18/DCDC_OUT/VDDCORE block on pins 38-42.
⚠️ If a module ever ships with a BL616 die instead, **dual Ethernet is off the
table** (BL616 has one EMAC, on different pads), so pin down the actual part
before designing two network ports.

Other module notes: the per-pin function lists in the spec show only ~6 alternates
each and **omit EMAC entirely** (note 3 defers to Ai-Thinker for the rest); the
pads are wired straight to the SoC, so EMAC works on the usual pins, but it is
worth confirming. Note 2 flags IO43-48 as *the* SDIO interface, needing
length-matching and impedance control.

### Verdict for "external flash + 2x EMAC + SDH + UART ISP + USB"

Fits, on either flash variant. Required IOs, all present on the module:

| Function | IOs | Module pins |
|---|---|---|
| EMAC0 | 16, 17, 18, 27, 28, 29, 30, 31, 32 | 8, 6, 5, 7, 4, 12, 13, 14, 15 |
| EMAC1 | 0, 1, 2, 3, 4, 6, 7, 13, 14 | 31, 32, 33, 40, 39, 34, 35, 11, 10 |
| SDH | 43, 44, 45, 46, 47, 48 | 22, 23, 26, 28, 29, 30 |
| USB | 40, 41 | 24, 25 |
| Console / UART ISP | 11, 12 | 37, 36 |
| BOOT | 33 | 27 |
| **Total** | **29 IOs** | |

- **External-flash variant**: 31 usable IOs (37 - IO34-39), 29 used, **IO5 and
  IO15 spare** -- matching option C+ above, including its no-JTAG caveat.
- **Embedded-flash variant**: 37 usable, 29 used, **8 spare** (IO5, 15, 34-39) --
  enough to add all three CAN FD channels on IO34/35, 36/37, 38/39 (option A).

Flash is inside the module either way, so "external flash" here is a module
variant, not something to wire up. One caveat: the module is marketed as an SDIO
peripheral ("通信接口：标准 SDIO"), and SDIO *device* (SDU) shares IO43-48 with
SDH -- using it as an SD **host** means giving up the device role.

## Appendix: full matrix

Blank = not available on that function. I2S (3), I2C2 (6), PWM (16), JTAG (17,
26), UART3 (18), PEC (27) and the RF/clk-out functions are omitted -- they follow
the periodic rules above and cover essentially every pad.

| GPIO | QFN56 | QFN68 | SPI (1) | SFlash/PDM (2) | CANFD (4) | I2C0/1 (5) | UART (7) | EMAC (8) | CAM (9) | ADC/Touch (10) | SDH (0) / SDIO (12) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 44 | 53 | SPI0_SS | pdm_clk_o | CANFD0_RX | I2C0_SDA | UART_SIG0 | E1_REF_CLK | CAM0_DATA0 | ADC0_6 | irtx |
| 1 | 45 | 54 | SPI0_SCLK | pdm_0_in | CANFD0_TX | I2C0_SCL | UART_SIG1 | E1_TXD[0] | CAM0_DATA1 | ADC1_6 | irtx |
| 2 | 46 | 55 | SPI0_MISO | pdm_clk_o | CANFD1_RX | I2C1_SDA | UART_SIG2 | E1_TXD[1] | CAM0_DATA2 | ADC0_7 | irtx |
| 3 | 47 | 56 | SPI0_MOSI | pdm_0_in | CANFD1_TX | I2C1_SCL | UART_SIG3 | E1_RXD[0] | CAM0_DATA3 | vref_ext[0] | irtx |
| 4 | 48 | 57 | SPI1_SS | pdm_clk_o | CANFD2_RX | I2C0_SDA | UART_SIG4 | E1_RXD[1] | CAM0_HSYNC | ADC1_7/AUADC_- | irtx |
| 5 | 49 | 58 | SPI1_SCLK | pdm_0_in | CANFD2_TX | I2C0_SCL | UART_SIG5 | E1_RX_ERR | CAM0_VSYNC | ADC0_8/AUADC_7/xtal_- | irtx |
| 6 | 50 | 59 | SPI1_MISO | pdm_clk_o | CANFD0_RX | I2C1_SDA | UART_SIG6 | E1_TX_EN | CAM0_CLK | ADC1_8/touch_ch[0] | irtx |
| 7 | 51 | 60 | SPI1_MOSI | pdm_0_in | CANFD0_TX | I2C1_SCL | UART_SIG7 | E1_RX_DV | CAM0_DATA4 | ADC0_9/touch_ch[1] | irtx |
| 8 | - | 61 | SPI2_SS | pdm_clk_o | CANFD1_RX | I2C0_SDA | UART_SIG8 | – | CAM0_DATA5 | ADC1_9 | – |
| 9 | - | 62 | SPI2_SCLK | pdm_0_in | CANFD1_TX | I2C0_SCL | UART_SIG9 | – | CAM0_DATA6 | ADC0_10 | – |
| 10 | - | 63 | SPI2_MISO | pdm_clk_o | CANFD2_RX | I2C1_SDA | UART_SIG10 | – | CAM0_DATA7 | ADC1_10 | – |
| 11 | 5 | 5 | SPI2_MOSI | – | CANFD2_TX | I2C1_SCL | UART_SIG11 | – | – | ADC0_0/AUADC_0 | – |
| 12 | 6 | 6 | SPI3_SS | – | CANFD0_RX | I2C0_SDA | UART_SIG0 | – | – | ADC1_0/AUADC_1 | – |
| 13 | 7 | 7 | SPI3_SCLK | pdm_0_in | CANFD0_TX | I2C0_SCL | UART_SIG1 | E1_MDIO | – | ADC0_1/dc_tp_18/C_- | – |
| 14 | 8 | 8 | SPI3_MISO | pdm_clk_o | CANFD1_RX | I2C1_SDA | UART_SIG2 | E1_MDC | – | vref_ext[1]/touch_ch[2] | – |
| 15 | 9 | 9 | SPI3_MOSI | pdm_0_in | CANFD1_TX | I2C1_SCL | UART_SIG3 | – | CAM0_DATA0 | ADC1_1/touch_ch[3] | – |
| 16 | 10 | 10 | SPI0_SS | pdm_clk_o | CANFD2_RX | I2C0_SDA | UART_SIG4 | E0_TX_EN | CAM0_DATA1 | ADC0_2/touch_ch[4] | – |
| 17 | 13 | 13 | SPI0_SCLK | pdm_0_in | CANFD2_TX | I2C0_SCL | UART_SIG5 | E0_MDIO | CAM0_DATA2 | ADC1_2/touch_ch[5] | – |
| 18 | 14 | 14 | SPI0_MISO | pdm_clk_o | CANFD0_RX | I2C1_SDA | UART_SIG6 | E0_MDC | CAM0_DATA3 | ADC0_3/touch_ch[6] | – |
| 19 | - | 15 | SPI0_MOSI | pdm_0_in | CANFD0_TX | I2C1_SCL | UART_SIG7 | – | – | AUADC_2/touch_ch[7] | – |
| 20 | - | 16 | SPI1_SS | pdm_clk_o | CANFD1_RX | I2C0_SDA | UART_SIG8 | – | – | AUADC_3 | – |
| 21 | - | 17 | SPI1_SCLK | pdm_0_in | CANFD1_TX | I2C0_SCL | UART_SIG9 | – | – | – | – |
| 22 | - | 18 | SPI1_MISO | pdm_clk_o | CANFD2_RX | I2C1_SDA | UART_SIG10 | – | – | – | – |
| 23 | - | 19 | SPI1_MOSI | pdm_0_in | CANFD2_TX | I2C1_SCL | UART_SIG11 | – | – | dsi_dn0 | – |
| 24 | - | 20 | SPI2_SS | pdm_clk_o | CANFD0_RX | I2C0_SDA | UART_SIG0 | – | – | dsi_dp0 | – |
| 25 | - | 21 | SPI2_SCLK | pdm_0_in | CANFD0_TX | I2C0_SCL | UART_SIG1 | – | – | ADC1_5/dsi_dn1 | – |
| 26 | - | 22 | SPI2_MISO | pdm_clk_o | CANFD1_RX | I2C1_SDA | UART_SIG2 | – | – | ADC0_11/dsi_dp1 | – |
| 27 | 15 | 23 | SPI2_MOSI | pdm_0_in | CANFD1_TX | I2C1_SCL | UART_SIG3 | E0_REF_CLK | CAM0_HSYNC | ADC1_11/dsi_dn_clk | – |
| 28 | 16 | 24 | SPI3_SS | pdm_clk_o | CANFD2_RX | I2C0_SDA | UART_SIG4 | E0_TXD[0] | CAM0_VSYNC | ADC1_3/dsi_dp_clk | – |
| 29 | 17 | 25 | SPI3_SCLK | pdm_0_in | CANFD2_TX | I2C0_SCL | UART_SIG5 | E0_TXD[1] | CAM0_CLK | ADC0_4/dsi_dn2 | – |
| 30 | 18 | 26 | SPI3_MISO | pdm_clk_o | CANFD0_RX | I2C1_SDA | UART_SIG6 | E0_RXD[0] | CAM0_DATA4 | ADC1_4/AUADC_4/dsi_- | – |
| 31 | 19 | 27 | SPI3_MOSI | pdm_0_in | CANFD0_TX | I2C1_SCL | UART_SIG7 | E0_RXD[1] | CAM0_DATA5 | ADC0_5/AUADC_5/dsi_- | – |
| 32 | 20 | 28 | SPI0_SS | pdm_clk_o | CANFD1_RX | I2C0_SDA | UART_SIG8 | E0_RX_DV | CAM0_DATA6 | vref_ext[2]/dsi_dp3 | – |
| 33 | 22 | 30 | SPI0_SCLK | – | CANFD1_TX | I2C0_SCL | UART_SIG9 | E0_RX_ERR | CAM0_DATA7 | – | – |
| 34 | 23 | 31 | SPI0_MISO | SFlash_CS | CANFD2_RX | I2C1_SDA | UART_SIG10 | – | – | – | Data2 / SDIO_DAT2 |
| 35 | 24 | 32 | SPI0_MOSI | SFlash_D0 | CANFD2_TX | I2C1_SCL | UART_SIG11 | – | – | – | Data3 / SDIO_DAT3 |
| 36 | 25 | 33 | SPI1_SS | SFlash_D1 | CANFD0_RX | I2C0_SDA | UART_SIG0 | – | – | – | CMD / SDIO_CMD |
| 37 | 26 | 34 | SPI1_SCLK | SFlash_D2 | CANFD0_TX | I2C0_SCL | UART_SIG1 | – | – | – | CLK / SDIO_CLK |
| 38 | 27 | 35 | SPI1_MISO | SFlash_D3 | CANFD1_RX | I2C1_SDA | UART_SIG2 | – | – | – | Data0 / SDIO_DAT0 |
| 39 | 28 | 36 | SPI1_MOSI | SFlash_CLK | CANFD1_TX | I2C1_SCL | UART_SIG3 | – | – | – | Data1 / SDIO_DAT1 |
| 40 | 29 | 37 | SPI2_SS | pdm_0_in | CANFD2_RX | I2C0_SDA | UART_SIG4 | – | – | USB_DP | – |
| 41 | 30 | 38 | SPI2_SCLK | pdm_clk_o | CANFD2_TX | I2C0_SCL | UART_SIG5 | – | – | USB_DM | – |
| 42 | - | 39 | SPI2_MISO | – | CANFD0_RX | I2C1_SDA | UART_SIG6 | – | – | vref_ext[3] | – |
| 43 | 31 | 40 | SPI2_MOSI | SFlash_CS | CANFD0_TX | I2C1_SCL | UART_SIG7 | – | – | – | Data2 / SDIO_DAT2 |
| 44 | 32 | 41 | SPI3_SS | SFlash_D0 | CANFD1_RX | I2C0_SDA | UART_SIG8 | – | – | – | Data3 / SDIO_DAT3 |
| 45 | 33 | 42 | SPI3_SCLK | SFlash_D1 | CANFD1_TX | I2C0_SCL | UART_SIG9 | – | – | – | CMD / SDIO_CMD |
| 46 | 35 | 44 | SPI3_MISO | SFlash_D2 | CANFD2_RX | I2C1_SDA | UART_SIG10 | – | – | – | CLK / SDIO_CLK |
| 47 | 36 | 45 | SPI3_MOSI | SFlash_D3 | CANFD2_TX | I2C1_SCL | UART_SIG11 | – | – | – | Data0 / SDIO_DAT0 |
| 48 | 37 | 46 | SPI0_SS | SFlash_CLK | CANFD0_RX | I2C0_SDA | UART_SIG0 | – | – | – | Data1 / SDIO_DAT1 |
