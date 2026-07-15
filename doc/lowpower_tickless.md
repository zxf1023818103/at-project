# Low-power WiFi retention + DTIM wake (bl616/BL618 & AmebaD/RTL8721D)

Both platforms can put the device into deep sleep **while staying associated to the AP**, waking on
DTIM beacons to service downlink traffic (e.g. an AWS IoT MQTT publish) and periodic uplink
(telemetry). The console `tickless <dtim>` command arms it on either platform. Both were verified
end-to-end on hardware: `tickless <n>` → the device deep-sleeps yet stays connected, and an AWS
publish to `bw20/<mac>/cmd` wakes it (`[AWS] RX publish` received) with telemetry still flowing.

The two platforms reach the same behaviour through **different mechanisms** — bl616 needed a whole
tickless framework ported in; AmebaD's is built into the RTL SDK — but expose the **same
`tickless <dtim>|off` command**.

---

## Quick reference

| | bl616 / BL618 | AmebaD / RTL8721D |
|---|---|---|
| Build flag for low power | `LP_FW=ON` | (built in; no flag) |
| Mechanism | lp_fw blob + FreeRTOS `configUSE_TICKLESS_IDLE` → `vApplicationSleep` → `bl_lp_fw_enter` → PDS15 | `freertos_pmu` CPU tickless + WiFi LPS (KM0 firmware) |
| Arm command | `tickless <dtim>` | `tickless <dtim>` |
| Disarm | `tickless off` | `tickless off` |
| Wake source | DTIM beacon / lp RTC | DTIM beacon |
| 32 kHz clock | internal RC32K (no ext xtal on this BL618) | RTL 32 kHz |
| Downlink-wake test | `make ... LP_FW=ON AWSOTA=ON` | `make AWSOTA=ON` |

**DTIM sizing — keep it small (default 1).** On the standard/basic path neither platform sends
keepalives during sleep, and 32 kHz timing drift accumulates, so a long DTIM risks missing beacons
or the AP aging the station out. Raise DTIM only with the retention features noted per platform.

---

## bl616 / BL618

### Build
```
make CHIP=bl616 BOARD=bl616dk LP_FW=ON            # low power only
make CHIP=bl616 BOARD=bl616dk LP_FW=ON AWS=ON     # + telemetry (uplink), fits 4 MB
make CHIP=bl616 BOARD=bl616dk LP_FW=ON AWSOTA=ON  # + cmd subscribe (downlink wake)
```
`LP_FW=ON` (default OFF; the OFF path is unchanged) turns on: the lp_fw blob (build-time concat +
runtime `bl_lp_init`), the RC32K clock bring-up (`HBN_Enable_RTC_Counter` + `app_clock_init`), and
the FreeRTOS tickless framework (`configUSE_TICKLESS_IDLE=1` via the `AT_LP_TICKLESS_IDLE` proxy).

`LP_FW=ON AWSOTA=ON` overflows the 4 MB layout's FW partition by ~68 KB → grow the FW region in
`component/bouffalo_sdk/bflb_image_tool/board_configs/bl616dk/partition_cfg_4M.toml`
(`size0 = 0x1F0000`; the die is 8 MB, FW then ends at 0x200000, still below PSM@0x300000).
`LP_FW=ON AWS=ON` (telemetry) fits the stock 0x178000.

### Console commands
- **`tickless <dtim>` / `tickless off`** — the WiFi-retention DTIM low power. This is the one to
  use for a connected low-power device. Sets `dtim_origin` + `wifi_mgmr_sta_ps_enter()` +
  `tickless_enter()`; only after this does the idle task ever enter PDS15.
- `lpsleep` — one lp_fw PDS15 entry (diagnostic). Enters cleanly, but its resume-without-reset
  vector is driven by the tickless framework, so standalone it does not self-wake; use `tickless`.
- `pdssleep` — bare PDS15 (no lp_fw), wake **resets** the chip after ~5 s (proven sleep+wake).

### Notes
- This BL618 board has **no external 32 kHz crystal** → falls back to the internal RC32K (~1200 ppm
  jitter). Keep DTIM small.
- A `-O3` miscompile of `lp_fw_save/restore_cpu_para` (asm hard-codes `a0` with no operand binding;
  inlining clobbers it) had to be fixed (`noinline` + `ATTR_TCM_SECTION` in `bl616_lp.c`), else
  `bl_lp_fw_enter()` hard-faults before sleeping.

---

## AmebaD / RTL8721D

### Build
```
cd project/amebad && make AWS=ON      # + telemetry
cd project/amebad && make AWSOTA=ON   # + cmd subscribe (downlink wake)
```
AmebaD's low power is **built into the RTL SDK** (`freertos_pmu` CPU tickless + WiFi LPS in the KM0
firmware) — there is no `LP_FW` flag; the `tickless` command drives the SDK mechanism directly.

### Console command (`tickless`, KM4-only)
Equivalent to the SDK's `ATXP=dtim,<n>` + `TICKPS r debug`:
- `tickless <dtim>` → `wifi_set_lps_dtim(n)` + `wifi_enable_powersave()` +
  `pmu_tickless_debug(ENABLE)` + `pmu_release_wakelock(PMU_OS)`.
- `tickless off` → `pmu_acquire_wakelock(PMU_OS)` + `wifi_disable_powersave()`.

`wifi_enable_powersave()` is needed because `src/main.cpp` disables power save on every associate
(`wifi_set_power_mode(0,0)` + `wifi_disable_powersave`). The command lives in
`component/amebad/monitor_port/monitor_cli.c`, gated `AT_AMEBAD_KM4` (wifi_* is in the KM4 wlan lib
and KM0 lacks a full libc; `monitor_cli.c` is shared across both cores).

### WiFi driver variant (`AMEBAD_WLAN_VARIANT`)
The KM0 firmware (`project_lp/.../lib_wifi_*_fw.a`) and the KM4 driver
(`project_hp/.../lib_wlan*.a`) are a **matched pair** — both must be the same variant.

| Variant | KM0 firmware + KM4 driver | For |
|---|---|---|
| standard (default) | `lib_wifi_fw.a` + `lib_wlan.a` | basic LPS/DTIM sleep+wake (verified) |
| **lt** (`AMEBAD_WLAN_VARIANT=lt`) | `lib_wifi_lt_fw.a` + `lib_wlan_lt.a` | **long-period tickless + WoWLAN keepalive** (long sleeps without the AP aging the station out) |
| lp | `lib_wlan_lp.a` | RF close/open + low-power connect timeouts (not sleep-retention) |

Start with the standard variant; move to **lt** for longer DTIMs (its keepalive is what mitigates
the "long DTIM → dropped" risk).

### Flashing gotchas (WSL bench)
- Flash **`build/amebad/firmware.bin`**, not `Image_All.bin` (the Makefile overrides build_image_all.py's
  `--out` to firmware.bin; `Image_All.bin` is the stale default output — check timestamps).
- Copy the **MP_ImageTool directory _and_ the image to a Windows-local path** (e.g. `C:\Users\Public\`).
  A Windows `.exe` run from a `\\wsl.localhost\…` UNC path fails to load its relative DLLs/`Setting.ini`.
- The app write-protects the firmware region at boot; the `flash unlock` step only works while the
  **app is running** (`SR1 14->00 SR2 40->00 OK`). Add **`-SkipModeCheck`** so the flash.ps1 0x15
  sniff doesn't contend with the ImageTool for COM5.
- The bootloader prefers **OTA2** (dual-slot); a normal flash writes OTA1 → the old OTA2 image still
  boots. Use **`-Erase`** to boot the new firmware (wipes LFS creds + TF-M ITS → reprovision:
  console `setenv WIFI_SSID/WIFI_PASSWORD`, then `aws_provision.ps1 -Endpoint/-Cert/-Key`; a
  non-STORAGE build keeps the key as a LittleFS DER file and the CA comes from the crt_bundle).
- Ports on this bench: **COM14 = control (CHIP_EN → `-BootPort`)**, **COM5 = flash + console @1.5 M
  (`-FlashPort`)**.

---

## AWS integration (both platforms)
- **Telemetry** (`AWS=ON`, `aws_telemetry.c`): uplink `bw20/<mac>/telemetry` continues during low
  power — the device wakes on its publish timer, sends, and sleeps again. No downlink (this client
  does not subscribe).
- **Downlink wake** (`AWSOTA=ON`, `aws_iot_demo.c`): the agent subscribes to `bw20/<mac>/cmd`.
  Publish to it (`aws iot-data publish --topic bw20/<mac>/cmd …`) → the AP buffers the frame for the
  PS station and delivers it at the next DTIM → the device wakes, the MQTT agent processes it, and
  it logs `[AWS] RX publish topic=bw20/<mac>/cmd payload=…`. Verified 4/4 on both bl616 and AmebaD.
  `<mac>` = the module BLE MAC (lower-case hex), printed at boot as `device (BLE MAC / Thing)`.
