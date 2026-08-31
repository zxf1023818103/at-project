# BL618DG Ethernet (EMAC v2 / GMAC)

Everything needed to bring up Ethernet on BL618DG: which MAC IP the SDK actually
drives, the full RMII pin map (cross-checked against the datasheet), what the PHY
layer supports, and the traps when porting EMAC code over from bl616/bl616cl.

Sources: `platform/bouffalo_sdk` (drivers/lhal, bsp/common/eth_phy,
bsp/board/bl618dgdk) and *BL618DG Datasheet v0.9.4* (2026-04-21), Table 3.1
"Pin definition" + §2.9.2 "EMAC".

## Two MAC IPs on the die, only one is driven

`drivers/lhal/config/bl618dg/device_table.c` lists **four** Ethernet devices:

| Device name | Base | `dev_type` | Driven by SDK |
|---|---|---|---|
| `BFLB_NAME_EMAC0` | `0x20083000` | `BFLB_DEVICE_TYPE_ETH` (v1 IP) | no |
| `BFLB_NAME_EMAC1` | `0x20089000` | `BFLB_DEVICE_TYPE_ETH` (v1 IP) | no |
| `BFLB_NAME_EMAC_V2_0` | `0x2008A000` | `BFLB_DEVICE_TYPE_EMAC_V2` | **yes** |
| `BFLB_NAME_EMAC_V2_1` | `0x2008C000` | `BFLB_DEVICE_TYPE_EMAC_V2` | **yes** |

All four share `BL618DG_IRQ_ETH`, and both IP flavours are muxed onto the *same*
pads. `drivers/lhal/CMakeLists.txt` compiles `src/bflb_emac_v2.c` only, so the v1
MACs have no driver. The board layer picks v2 explicitly:

```c
/* bsp/board/bl618dgdk/board_gpio.c */
volatile uint32_t *p = (uint32_t *)0x20000390;
*p |= (1 << 30);   /* select GMAC IP -- board_emac_gpio_init()  == EMAC0 pads */
*p |= (1 << 31);   /* select GMAC IP -- board_emac1_gpio_init() == EMAC1 pads */
```

## Pin map

RMII only, 9 mandatory signals per MAC + an optional RX_ER. GPIO Function = **8**
(`GPIO_FUNC_EMAC`) for every pin below. "board" = configured by
`board_emac_gpio_init()` / `board_emac1_gpio_init()`.

### EMAC0 -> `EMAC_V2_0` (`0x20000390` bit30)

| Signal | GPIO | QFN56 | QFN68 | board |
|---|---|---|---|---|
| E0_TX_EN | 16 | 10 | 10 | yes |
| E0_MDIO | 17 | 13 | 13 | yes |
| E0_MDC | 18 | 14 | 14 | yes |
| E0_REF_CLK | 27 | 15 | 23 | yes |
| E0_TXD[0] | 28 | 16 | 24 | yes |
| E0_TXD[1] | 29 | 17 | 25 | yes |
| E0_RXD[0] | 30 | 18 | 26 | yes |
| E0_RXD[1] | 31 | 19 | 27 | yes |
| E0_RX_DV | 32 | 20 | 28 | yes |
| E0_RX_ERR | 33 | 22 | 30 | **no** (optional) |

### EMAC1 -> `EMAC_V2_1` (`0x20000390` bit31)

| Signal | GPIO | QFN56 | QFN68 | board |
|---|---|---|---|---|
| E1_REF_CLK | 0 | 44 | 53 | yes |
| E1_TXD[0] | 1 | 45 | 54 | yes |
| E1_TXD[1] | 2 | 46 | 55 | yes |
| E1_RXD[0] | 3 | 47 | 56 | yes |
| E1_RXD[1] | 4 | 48 | 57 | yes |
| E1_RX_ERR | 5 | 49 | 58 | **no** (optional) |
| E1_TX_EN | 6 | 50 | 59 | yes |
| E1_RX_DV | 7 | 51 | 60 | yes |
| E1_MDIO | 13 | 7 | 7 | yes |
| E1_MDC | 14 | 8 | 8 | yes |

Notes:

- The gap at GPIO5 in `board_emac1_gpio_init()` is not an omission: that pad is
  `E1_RX_ERR`, and RMII RX_ER is optional (LAN8720 & friends usually leave it
  unconnected). Add GPIO5 / GPIO33 only if the PHY drives RX_ER and you want the
  error counters.
- EMAC1 occupies GPIO0-7, a dense block of low pads -- check it against boot
  straps and other peripherals when laying out a board.
- The AP console (UART0 TX = GPIO11 @ 2 Mbps) collides with neither group.
- `bflb_emac_v2.c` can bit-bang MDIO over GPIO instead of the GMII_ADDR/GMII_DATA
  registers (`BFLB_EMAC_MDIO_USE_GPIO`, default 0). Its defaults are MDC=GPIO18 /
  MDIO=GPIO17, i.e. the EMAC0 pair; the commented alternates 14/13 are EMAC1's.

## Speed: 10/100 only

Datasheet §2.9.2: the two MACs are IEEE 802.3 **10/100 Mbps, RMII only**, and the
interface clock must come from the PHY's 50 MHz output. Features called out:
IEEE 1588, VLAN, and a built-in descriptor-based DMA.

`bflb_emac_v2_gmii_select()` and `EMAC_CMD_SET_SPEED_1000M` exist because the
underlying IP is a Synopsys DWMAC-style core, but BL618DG brings out no
GMII/RGMII pads -- **1000M is unreachable on this chip**; treat that path as dead
code.

Because REF_CLK comes from the PHY, examples set `clk_internal_mode = false`,
which makes `bflb_emac_init()` take the `regval |= (1<<4)` / `&= ~(1<<5)` branch
(EMAC0) or bits 16-20 (EMAC1) of the GLB register at `0x20000390`.

## PHY support (shared with bl616/bl616cl)

`bsp/common/eth_phy/` is chip-independent and BL618DG uses it unchanged. Exactly
two drivers exist:

| Driver | Match | Covers |
|---|---|---|
| LAN8720 (`ephy_lan8720.c`) | ID `0x0007c0f0`, mask `0x0000000f` | Microchip LAN8720/8720A |
| General (`ephy_general.c`) | fallback when no ID matches | any IEEE 802.3 Clause-22 PHY |

`phy_drv_table[]` in `eth_phy.c` holds the single LAN8720 entry; `eth_phy_scan()`
walks MDIO addresses 0-31, reads ID1/ID2, and falls back to General with a
`drv_match falied` warning. RTL8201F / DP83848 / YT8512 / KSZ8081 / IP101 have no
dedicated driver but work through General (auto-negotiation, link status,
speed/duplex, soft reset, loopback, power-down). The porting layer is three
functions: `eth_phy_mdio_read/write` + `eth_phy_delay_ms`.

To add a model: copy `ephy_lan8720.c`, fill in `phy_id`/`phy_id_mask`/`phy_name`,
delegate `phy_init`/`phy_ctrl` to the General implementations where possible, and
append to `phy_drv_table[]`.

## Porting traps from bl616/bl616cl

`bflb_emac_v2.c` re-exports the common API (`bflb_emac_init`, `queue_tx_push`,
`queue_rx_push`, `md_read/write`, `feature_control`, `irq_attach`), so
`eth_phy` and `lwip_netif_emac` compile as-is. But:

1. **`bflb_emac_feature_control()` implements only 8 commands** on v2: `SET_TX_EN`,
   `SET_RX_EN`, `SET_FULL_DUPLEX`, `SET_PHY_ADDRESS`, `SET_SPEED_1000M/100M/10M`,
   `GET_TX_DB_AVAILABLE`, `GET_RX_DB_AVAILABLE`. Everything else returns `-EPERM`:
   `SET_RX_PROMISCUOUS`, `SET_TX_AUTO_PADDING`, `SET_TX_CRC_FIELD_EN`,
   `SET_RX_HUGE_FRAME`, `GET_TX_BD_PTR`, `GET_RX_BD_PTR`, ...
   Use the fine-grained `bflb_emac_v2_*` calls instead (e.g.
   `bflb_emac_v2_promisc_enable()`).
2. **The LAN8720 rx_clk inversion is a no-op on v2.** `emac_basic/main.c` still
   calls `bflb_emac_feature_control(emacx, EMAC_CMD_SET_MAC_RX_CLK_INVERT, true)`
   and ignores the return value -- on v2 that hits `default: -EPERM`. Clock
   inversion is done in `bflb_emac_init()` from `clk_internal_mode` writing
   `0x20000390`. If timing is off with a LAN8720, tune those GLB bits, not that
   call.
3. **MDIO divider differs**: `md_clk_div = 79` on bl616cl/bl618dg vs 39 on bl616.
4. **D-cache maintenance**: TX buffers need
   `bflb_l1c_dcache_clean_range()` before handing the descriptor to the DMA
   (`EMAC_V2_DO_FLUSH_DATA` + `defined(BL618DG)` in `bflb_emac_v2.c`). The v1
   chips have no such step.
5. `EMAC_SPEED_10M_SUPPORT` is 1 only for bl616cl/bl618dg; the bl616 branch of
   `emac_basic` hits `LOG_E("10M speed not supported") + while(1)`.
6. Both MACs share one IRQ, so a dual-port build must demux in the ISR.

## v1 vs v2 at a glance

| | v1 `bflb_emac.c` (bl702/bl616/bl616cl) | v2 `bflb_emac_v2.c` (bl618dg) |
|---|---|---|
| IP lineage | OpenCores `ethmac` (`MODER@0x0`, `MIICOMMAND@0x2C`, BDs at `0x400`) | Synopsys DWMAC/GMAC (MAC CSRs @`0x0`, DMA CSRs @`0x1000`) |
| Size | 834 lines / 29-line header | 4302 lines / 1751-line header (~150 APIs) |
| Descriptors | 8-byte BDs inside register space, max 64 | DWMAC ring in RAM, dual-buffer + chain |
| Speed | 100M (10M only on bl616cl) | 10/100 (GMII path unusable here) |
| Checksum offload | none | TX IPv4/TCP/UDP, RX IPC w/ extended status |
| PTP 1588 | none | v1/v2 snoop, per-packet timestamps, addend tuning |
| Filtering | basic | 64-bit hash + perfect, SA/DA, multicast/unicast |
| Flow control | none | 802.3x pause TX/RX, unicast pause detect |
| Other | -- | jumbo frames, PMT wake, LPI/EEE, AVB slots |
