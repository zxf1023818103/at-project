# BL602 首次上板 bring-up — 进行中

跨机/跨日 handoff 用。建议从这页接着调，调通后整段删掉就行。

**最新状态**（2026-05-24 21:xx 截止）：build 干净 (0 warning)，BL602
`at_bl602_whole_flash_data.bin` 已 commit/push 到
`origin/experiment/driver-only`，烧到 bl602dk 上**串口零输出**（连 boot2 banner 都没有）。

## 现场参数

- 板子：bl602dk，USB↔UART 走板载 CH340
- COM 口：COM64 @ **2,000,000** baud, 8N1（已确认）
- 烧录工具：BLDevCube GUI（Windows）
- 烧录完复位方式：拔插 USB
- JLink 没接（S/N 601023163, JTAG, `-device RISC-V`，可备用，需要时插上）

## 已排除

- ✗ baudrate 错（已确认 2 Mbps）
- ✗ console pin 错（实际 build 走 UART0 GPIO4 TX / GPIO3 RX，跟 upstream
  `platform/bouffalo_sdk/bsp/board/bl602dk/board.c` 完全一致；CLAUDE.md 里
  "UART1 (test/secondary) GPIO4/3 @ 115200" 那行是错的，同对 pin 不会在两条
  UART 上）
- ✗ bin 头/分区表 layout 不对 — 已 hexdump 验证：
    - 0x0000 = `BFNP`（boot2 fw_header）
    - 0xe000 = `BFPT`（partition table, FW slot at 0x10000 size 0x178000）
    - 0x10000 = `BFNP`（app fw_header）

## 最可能 root cause

**BLDevCube 把 `at_bl602_whole_flash_data.bin` 烧到了错的地址**（多半是
0x10000 当 FW image 烧）。这个 .bin 是整张 4 MB flash 镜像；烧到 0x10000
→ 0x0..0xe000 该有 boot2 的位置是空 → bootROM 找不到 boot2 → 整段静默
（包括 boot2 自己的 banner，跟现象吻合）。

## 下次起手按这个顺序走

1. **重新烧，地址改对**。两条路二选一：

    **A. 单文件，addr `0x0`**（最省事）：
    ```
    at_bl602_whole_flash_data.bin → 0x000000
    ```

    **B. 分文件，IoT mode 标准烧法**（更清晰）：

    | File（都在 `project/bouffalo/build_bl602/`） | Address |
    |---|---|
    | `boot2_bl602_release_v8.1.3.bin` | `0x000000` |
    | `partition.bin` | `0x00e000` |
    | `partition.bin`（备份槽） | `0x00f000` |
    | `at_bl602.bin` | `0x010000` |

    （`mfg_bl602_gu_2e8b4aa47_40m_v2.90.bin` / `efusedata*.bin` 先不用烧 —
    mfg 是 OTA 才用；efuse 默认值多半不需要重写。）

2. **烧完务必松开 BOOT 键再复位**。按着 BOOT 拔插 USB 永远进 ROM
   download mode，app 不会跑。

3. **上电后期望看到的**：
    - 先是 boot2 banner（"Boot2 Version : 1.x.x" 之类）
    - 紧跟 app 第一条 print：`dynamic memory init success\r\n  ocram heap size: N Kbyte`
    - 然后 `Version of used components:`、`cgen1:NNNNNNNN`、`board init done`
    - 走到 `vTaskStartScheduler()` 之后就是各 task 自己 print

4. **如果地址都对了仍然静默** → 接 JLink halt 看 PC 定位是 ROM / boot2 / app
   哪一段：
    ```
    JLink -device RISC-V -if JTAG -speed 4000 -autoconnect 1
    halt
    regs       # 看 pc:
               #   0x21000000 段 = BootROM
               #   0x22000000 段 = boot2 SRAM
               #   0x23000000+ 段 = app XIP flash
    ```
    PC 在 ROM 段 = bootROM 没成功跳出去（boot2 没烧 / 烧错地址）。
    PC 在 boot2 段 = boot2 跑着但没移交 app（partition 没烧 / app fw_header 不对）。
    PC 在 app 段 = app 在跑但卡在 printf 之前的 init（system_clock_init /
    peripheral_clock_init 之类），需要 step 出来。
    nTRST 复位用 JLink commander：`ClrTRST; Sleep 100; SetTRST`（不需要 power cycle）。

## 调通之后

- 删掉这页文件
- 在 [component_migration_status.md](component_migration_status.md) 末尾"下次继续要看的"
  里把 #1 BL616/BL616CL runtime 那条 promote 上来
- memory 里 `bl602-first-boot-in-progress` 那条转成"closed"状态或直接删

## 相关 commit

```
e1675af component/bflb_power_mgmt: wire CONFIG_PM_MANAGER deps + doc handoff
5ac51c9 build: clean up build warnings on BL602 / BL616 / BL616CL (164/194/194 -> 0)
1d682ac build: auto-regen AtCommandParser.rl.cpp via CMake ragel rule
```

工作目录还留着的（**都不是这次 session 改的，不要 commit**）：
- `component/zlib (modified content)` — submodule
- `tool/bflb_tools/bouffalo_flash_cube/chips/{bl602,bl616,bl616cl}/eflash_loader/eflash_loader_cfg.ini` — vendor 工具自更新
- `.claude/scheduled_tasks.lock` — Claude session lock
- `platform/BL602_SDK/` — untracked
