# AmebaD: port to TizenRT's newer WiFi driver (lib + osdep + headers) to fix the TCP-TX wedge

Branch: `experiment/amebad-tizenrt-wifi-lib` (off `3ceb4ac` — iperf3 integrated, core functions OK,
high-rate device-as-client TCP-TX wedges).

## Why

The high-rate device-as-client **TCP-TX wedge** (whole chip hard-locks after ~1-3 s of sustained
uplink) is a **race in the closed `lib_wlan.a` TXBD-ring submit** (`rtw_dump_xframe`: the descriptor
write + host-index advance is not atomic vs the ISR-context mgmt submit → ring/sema corruption →
WL_DMA ISR fault). Reproduces on the stock Realtek SDK image, so it is a driver/firmware defect, not
our port.

**Realtek FIXED it in a newer driver.** Samsung **TizenRT** (`/home/zxf10/TizenRT`,
`os/board/rtl8721csm`) ships the **same RTL8721D va0 silicon/ROM** with a **~11-month-newer (2026-05)**
`lib_wlan_fpu.a` / `lib_wps_fpu.a` whose `rtw_dump_xframe` now **brackets the ring submit in
`save_and_cli`/`restore_flags`** (PRIMASK IRQ-disable) — exactly the fix. (Our older lib uses only
`rtw_enter_critical`, a spinlock, no IRQ mask.)

Two cheaper routes were tried and rejected (see memo `project_amebad_iperf3_component` ROUNDs 13-14):
- **Replicate just the `save_and_cli`** on our lib (via `--wrap` / a runtime inline hook): doesn't
  work — the fix is intertwined with the newer driver's broader rework (the `xmit_priv` struct moved
  `0xb48/0xb4c/0xb5c`→`0xa00/0xa04/0xa14`, `UpdateTxbdHostIndex` was rewritten), so the visible
  one-liner can't be cherry-picked.
- **Drop-in the newer lib over our old osdep/headers**: links + boots the scheduler, but **hard-locks
  at WiFi init** in `vApplicationMallocFailedHook`. **Root-caused via OpenOCD heap instrumentation:**
  NOT memory budget (254 KB heap, only ~30 KB used) and NOT `WIFICFG_TypeDef` (byte-identical) — it is
  **HEAP CORRUPTION**: the free-list is trashed at ~allocation #54-55 (free stops decreasing, then
  *increases*) during the lib's `rtw_drv_entry`, where it writes a **520-byte struct out of bounds**.
  That is an **ABI struct-size mismatch**: a struct shared across the lib↔glue/header boundary is laid
  out differently in the newer driver than in our older-derived osdep/headers.

**Conclusion / this plan:** to use the newer lib we must align **the whole osdep + WiFi-driver header
set** (the struct definitions the lib shares with our glue) to the newer driver — not patch one
struct. Bring TizenRT's matching osdep + headers.

## Already solved (link-level integration — re-apply from the ROUND-14 work)

These were proven during the drop-in attempt and should be reused verbatim:
- **ROM crypto (92 syms)**: the newer lib's WPA/SAE path needs `rtl_aes_*`/`rtl_mpi_*`/`rtl_ecp_*`/
  `rtl_md5_*`. Generate `component/amebad/app_km4/rom_symbol_crypto.ld` from TizenRT
  `build/configs/rtl8721csm/scripts/rlx8721d_img2_is.ld` (same va0 ROM addresses), `cat` it into the
  combined KM4 ld. `rom_ssl_ram_map`/`p_rom_ssl_ram_map` already provided by `mbedtls_rom.ld` + inited
  by `rom_mbedtls.c`.
- **Config/glue compat (~20 syms)** the newer lib expects the integrator to define (the old lib defined
  them internally): `component/amebad/wlan/src/amebad_wlan_compat.c` — `g_tx_ampdu_enable`,
  `rtw_bw_mode`, `rtw_power_mgnt`, `gk_rekey[_time]`, `rtw_ant_div_gpio_ext`, `softap_keep_silent_*`,
  `rtw_anti_interference_en`, `rtw_auto_adaptivity_en`, `rtw_cbw40_enable`,
  `rtw_set_custom_country_code_en` (all default 0); glue fns `rltk_set_pmf`, `rltk_wlan_get/set_link_err`,
  `rtw_wifi_connect_monitor_mgnt`, `rtw_printf[_info]`, `sys_reset`.
- **`wifi_config`**: the newer lib renamed `rtk_wifi_config`→`wifi_config`. Add
  `extern WIFICFG_TypeDef wifi_config __attribute__((alias("rtk_wifi_config")));` in
  `usrcfg/rtl8721d_wificfg.c`. (`WIFICFG_TypeDef` itself is identical — verified.)
- **Float ABI**: TizenRT libs are **soft(fp)-float**, our fw is **hard-float**. Use the **`_fpu`**
  variants (`lib_wlan_fpu.a`, `lib_wps_fpu.a`) and add `-Wl,--no-warn-mismatch` to the KM4 link (WiFi
  API is all int/ptr/struct; only float-arg boundaries matter, and there are none).
- **Firmware**: the WiFi-MAC firmware is the **separate KM0 `lib_wifi_fw.a`**, NOT in `lib_wlan.a`. The
  fix is host-side, so **do not** swap the KM0 firmware initially (verify the driver↔firmware protocol
  still handshakes at boot; if it errors, that's a separate, observable failure).

## Approach: align osdep + WiFi-driver headers to the newer driver

TizenRT's WiFi driver source tree (headers are what we need; the lib stays binary):
- osdep: `component/os/os_dep/include/osdep_service.h` + `component/common/drivers/wlan/realtek/src/osdep/`
  (`skbuff.h`, `wlan_intf.h`, `wireless.h`, `bt_intf.h`, `freertos/wrapper.h`).
- WiFi driver internal/public headers: `component/common/drivers/wlan/realtek/include/` and
  `.../src/include/` (the `rtw_*` struct headers: `xmit_priv`, `recv_priv`, `_adapter`,
  `sta_info`, `mlme*`, `wlan_bssdef`, `wifi_constants/structures`, `autoconf.h`).

Our equivalents are vendored flat under `component/amebad/osdep/`, `component/amebad/wlan/` (+ its
`osdep/`, `rom/`, `src/`), `component/amebad/wifi/`. These are an **older snapshot**; some struct
defs diverged (e.g. `osdep_service.h` 1449→1456 lines; `task_struct` shifted; `xmit_priv` layout
changed). The 520-byte corruptor is one such struct — to be identified in Stage 0.

The osdep `.c` (`freertos_service.c`, `osdep_service.c`) implement the osdep over **our**
FreeRTOS(+TCP) — keep our implementations but ensure the **struct layouts/sizes** they use match the
newer headers. The seam to the IP stack (`lwip_intf.c` → FreeRTOS-Plus-TCP via `realtek_netif.c`)
stays ours (TizenRT uses lwIP; we don't).

## Staged plan (each stage verified by the heap-corruption harness)

**Re-add the diagnostic harness first** (temporary, in `freertos_heap_tlsf.c`): on `pvPortMalloc`
failure capture `{size, caller, kfree_size(0)}`, and log per-alloc `{size, kfree_size after}` to an
array. Plus the per-task `{stack_size, name}` log in `_freertos_create_task`. This is how each stage
is verified to be corruption-free (free decreases monotonically; no `vApplicationMallocFailedHook`).

- **Stage 0 — identify the divergent struct(s) — DONE (2026-06-21). Corruptor named: it is NOT a
  data struct, it is the osdep function-pointer table `struct osdep_service_ops`.**
  - **Method:** `nm` showed both libs import `U osdep_service` (the table is read by the lib at
    compile-time-baked offsets). Diffed our `osdep_service.h` table against TizenRT's
    `os/board/rtl8721csm/src/component/os/os_dep/include/osdep_service.h`, stripping our `#if 0`
    block. Result: **the compiled tables are identical for members 1..90; TizenRT appends exactly
    ONE member at the end — `rtw_zmalloc_32aligned` (index 91, byte offset 360).**
  - The summary's earlier "4 extra `*_delayed_work` shift the table" theory was **wrong** — those 4
    members are inside `#if 0 //TODO` in our header (lines 1394-1399) and TizenRT's, so they are
    compiled out in **both**; they never occupied a slot. The only real divergence is the appended
    `rtw_zmalloc_32aligned`.
  - **Mechanism (unifies with the ROUND-14 heap-corruption / "520-byte OOB write"):** the newer lib
    calls `osdep_service.rtw_zmalloc_32aligned(sz)` for a DMA/cache-aligned buffer (~520 B). Our old
    90-member table ends at offset 356, so the lib reads offset 360 — **one word past the end of our
    `osdep_service` instance** — as the function pointer → garbage call → garbage return pointer →
    the lib writes its ~520-byte struct to a bad address → the free-list trashing the harness caught
    at alloc #54-55. The missing table member **is** the corruptor.
  - **Fix applied (offset-preserving, additive — no existing slot moves):**
    - `osdep_service.h`: appended `u8* (*rtw_zmalloc_32aligned)(u32 sz);` as the last table member.
    - `freertos_service.c`: added `_freertos_zmalloc_32aligned` (`kmemalign(sz, 32)` + zero; the block
      is a normal tlsf_config kmalloc block, so the lib's `rtw_mfree`->`vPortFree` frees it) and the
      matching instance entry.
  - **Scoping (new lib's genuinely-new external imports vs our old lib — `nm` delta):** only two
    buckets, both already-known: (1) **ROM crypto** `rtl_aes_*`/`rtl_mpi_*`/`rtl_ecp_*`/`rtl_md5_*` +
    `rom_ssl_ram_map`/`p_rom_ssl_ram_map` (the `rom_symbol_crypto.ld`); (2) **`wifi_config`** (the
    alias). The `phydm_nhm_*`/`rtw_xmit_data`/`rtw_recv_indicatepkt_sc`/`rtw_get_rx_sc_free_entry`/
    `rtw_indicate_sta_handshake_done`/`rtw_coex_ble_scan_duty_update`/`skb_assign_buf`/`kfree_skb`
    symbols are **defined inside `lib_wlan_fpu.a` itself** (intra-archive) — not missing glue. The
    standard `pvPortMalloc`/`vPortFree`/`vPortYield`/`vQueueDelete`/`xQueueSemaphoreTake` we already
    provide. So **Stages 1-2 (bulk osdep + wifi-driver header import) look UNNECESSARY** — the single
    osdep table member was the one osdep ABI divergence. Go straight to Stage 3 (swap + relink) then
    Stage 4 (boot) to empirically confirm sufficiency; if boot still corrupts, hunt the next struct
    with the harness as originally planned.
- **Stage 1 — import TizenRT's osdep headers.** Replace `component/amebad/osdep/osdep_service.h` (and
  `skbuff.h`/`wrapper.h`/`wlan_intf.h`/`wireless.h` under `wlan/osdep/`) with TizenRT's versions.
  Reconcile compile breaks against our `freertos_service.c`/`osdep_service.c` and our FreeRTOS config.
  Rebuild the osdep/glue libs only; fix struct/macro mismatches.
- **Stage 2 — import TizenRT's WiFi-driver headers** (`autoconf.h`, `wifi_constants/structures`,
  `wlan_bssdef`, and the `rtw_*` internal struct headers that define the divergent structs). These are
  pulled by many TUs (`amebad_sdk_includes`) — expect ripple; reconcile.
- **Stage 3 — swap the libs + re-apply the link-level fixes — DONE (2026-06-21): clean link.**
  In place now:
  - `component/amebad/wlan/lib_wlan.a` <- TizenRT `lib_wlan_fpu.a` (19 MB); `component/amebad/
    wpa_supplicant/lib_wps.a` <- `lib_wps_fpu.a` (1.7 MB). (`rtw_dump_xframe` now at RAM 0x1001145c,
    was 0x1000f1fc — confirms the new binary.)
  - `component/amebad/app_km4/rom_symbol_crypto.ld` — 92 `PROVIDE(rtl_* = 0x...)` from TizenRT's
    `rlx8721d_img2_is.ld`; cat into the combined KM4 ld (app_km4/CMakeLists.txt). `rom_ssl_ram_map`/
    `p_rom_ssl_ram_map` come from the existing `mbedtls_rom.ld` (already cat'd) — not duplicated.
  - `component/amebad/wlan/src/amebad_wlan_compat.c` (compiled into `amebad_wlan_glue`): the new-only
    external set the linker actually demanded, in three passes:
      1. `rtw_printf`/`rtw_printf_info` (real, -> vprintf); WPS `g_wps_context` (256 B zeroed backing) +
         `wpas_get_wifi_mac` (stub); WoWLAN `rs8721d_set_*_cmd` ×5 (stubs).
      2. config globals our `wifi_util.c` references that the old lib exported (the new lib externalizes):
         `g_tx_ampdu_enable`, `rtw_bw_mode`, `rtw_cbw40_enable`, `rtw_power_mgnt`, `rtw_ant_div_gpio_ext`,
         `rtw_anti_interference_en`, `rtw_auto_adaptivity_en`, `rtw_set_custom_country_code_en`,
         `gk_rekey`(u8)/`gk_rekey_time`(u32), `softap_keep_silent_table_enable`(u8)/`_interval`(int) — all
         default 0 (the `wext_set_*` setters write them at config time); glue fns `rltk_set_pmf`,
         `rtw_wifi_connect_monitor_mgnt`.
      3. `rltk_wlan_get/set_link_err` (static u32 store); `sys_reset` (self-contained SYSRESETREQ — the
         real mbed `sys_api.c` is not compiled in; error-path-only).
  - `usrcfg/rtl8721d_wificfg.c`: `extern WIFICFG_TypeDef wifi_config __attribute__((alias("rtk_wifi_config")));`
  - `-Wl,--no-warn-mismatch` on the KM4 link (the `_fpu` libs are `-mfloat-abi=softfp`, our fw hard-float;
    link-compatible, no float args cross).
  - `osdep_service.h` + `freertos_service.c`: the Stage-0 `rtw_zmalloc_32aligned` table member +
    `_freertos_zmalloc_32aligned` (kmemalign 32 + zero). Both in the final image (T symbols).
  - **NOT yet done — `save_and_cli`/`restore_flags` still `void`/`void` over `taskENTER_CRITICAL` (BASEPRI).**
    The new lib's TX-ring fix brackets `rtw_dump_xframe` in these and its ABI is `unsigned int
    save_and_cli(void)` / `restore_flags(unsigned int)` with PRIMASK (TizenRT `tizenrt_service.c`
    `irqsave`/`irqrestore`). Our void/void taskENTER pair links fine (lib ignores the garbage return /
    we ignore the passed flag) and is OK for boot+connect (mgmt TX is task-context there), but for the
    Stage-5 TX-wedge test it must be upgraded to the PRIMASK save/restore signature (ISR-safe) to
    faithfully reproduce the fix — ~15 call sites across freertos_service.c/osdep_service.c/lwip_intf.c/
    wrapper.h(skb_queue_tail). Deferred to Stage 5.
  - Build: `make IPERF=ON` -> clean link, fresh `build/amebad/firmware.bin` (659456 B) +
    `km0_km4_image2.bin`. NOTE: flash `firmware.bin` (or the 3-piece), NOT the stale 4 MB `Image_All.bin`.
- **Stage 4 — boot + connect — ATTEMPTED 2026-06-21: STILL HANGS (osdep-table fix necessary but NOT
  sufficient).** Flashed `firmware.bin`; console is silent. OpenOCD one-shot (KM4): PC = 0x0e0595fc
  **inside `vApplicationMallocFailedHook`** (0x0e0595e8), `uxCurrentNumberOfTasks = 4` (scheduler IS
  up), CFSR/HFSR/SFSR all 0 (no fault). So it boots, starts the scheduler, then a `pvPortMalloc`
  returns NULL during WiFi init — **exactly the ROUND-14 hang**. The Stage-0 `rtw_zmalloc_32aligned`
  member was real but did not crack it: another struct's ABI still feeds a bad allocation size (or an
  OOB write trashes the free-list, per the ROUND-14 "520-byte struct" finding). Next: re-add the
  pvPortMalloc size/caller harness, ID the exact struct (recv_priv / xmit_priv / cmd_priv / evt_priv /
  sta_priv), then either align that one header or proceed to the bulk header import (Stage 1-2).
  **GATE: clean boot + connect = the ABI is aligned.**

  **2026-06-21 bisection (heap harness in pvPortMalloc: ring of {size, caller, free} +
  stack-scan of the call chain at the corruption point). Findings:**
  - Hang = heap free-list corruption during init (free decreases normally to ~228 KB then
    a small alloc fails with kfree_size=0). Scheduler up (4 tasks), no CPU fault.
  - The ROUND-14 "520-byte struct" is a **LittleFS** cache buffer, NOT a WiFi struct: the
    stack chain at the 0x208 alloc was `lfs_selftest -> lfs_mount/lfs_bd_flush -> malloc(520)`.
    Misdiagnosis corrected.
  - Disabling `lfs_selftest` (main.c, temporary `#if 0`) did NOT fix it — the corruption just
    moved to the next heavy heap user. Stack chain at the new corruption point (idx 49):
    **`wifi_bringup_thread -> wifi_on(+90) -> _freertos_init_sema -> xQueueCreateCountingSemaphore
    -> pvPortMalloc`**. So the corruptor IS the WiFi lib's `wifi_on`/`rtw_drv_entry` init.
  - `wifi_bringup_thread` stack = 16 KB (main.c:639, `xTaskCreate(...,4096,...)` words) — ample,
    so not an obvious stack overflow. `.bss`(ends 0x1001c670) does not overlap the heap
    (0x1003c820..0x1007c000, 254 KB). `_freertos_create_task` stack-size unit (words) matches
    TizenRT's `_tizenrt_create_task` (`stack_size *= 4` for its byte-based pthread API) — not a
    unit mismatch. So the remaining cause is an **ABI struct mismatch in the wifi_on /
    rtw_drv_entry path** — a struct our osdep/headers lay out smaller than the newer driver, so
    the lib writes past it (or stores a handle at a wrong offset) -> OOB into the heap. This
    confirms Stage 1-2 (import TizenRT's WiFi-driver headers) is the real fix; the Stage-0 osdep
    table member was necessary but the *data* structs also diverge.
  - Harness state left in the tree (REMOVE when done): `freertos_heap_tlsf.c` ring + stack-scan,
    `osdep_service.c`/`freertos_service.c` `HH_CALLER`, and `main.c` `lfs_selftest` `#if 0`.
- **Stage 5 — the payoff: TCP-TX.** `iperf -c <host> 5201 30` — the wedge should be **gone** (the
  newer lib's `save_and_cli` ring-submit fix). Then regression: `iperf -s` (RX), `-u` (UDP), WiFi
  reconnect, multi-minute soak. Remove the diagnostic harness.

## Risks
- **Osdep/header ripple**: the headers feed ~dozens of TUs on both cores (autoconf.h, sdk_includes) —
  importing newer versions may break unrelated builds (KM0, the no-WiFi minimal image). Reconcile per
  build variant.
- **More than one divergent struct**: the 520-byte one is the first corruptor the harness caught;
  there may be others downstream (the harness re-runs catch them stage by stage — loop until clean).
- **osdep `.c` vs newer headers**: our `freertos_service.c` is written against the old osdep contract
  (e.g. `_freertos_create_task` stack-size unit = words). If the newer osdep changed a contract,
  reconcile (the harness's per-task `stack_size` log flags unit/inflation mismatches).
- **driver↔KM0-firmware protocol**: keeping the old `lib_wifi_fw.a` assumes a stable H2C/C2H + ring
  protocol. If boot's firmware-download handshake fails, bring TizenRT's matching firmware too (locate
  it in the TizenRT KM0/LP build).
- **Effort**: this is a driver-layer upgrade — realistically multi-day, with the header reconciliation
  the bulk of it. The gates (Stage 0 struct ID, Stage 4 clean boot) keep it incremental.

## Verification
- Per-stage harness checks (heap monotonic, no malloc-fail) — Stages 1-4.
- Final: `make IPERF=ON`, flash (`flash.ps1`, COM14 boot / COM3 data), reset → WiFi connect (DHCP) →
  `console.ps1 -Send 'iperf -c 192.168.4.250 5201 30'` runs the **full 30 s with no wedge / no fault**.
  Regression: `iperf -s` ~9.2 Mbit/s RX, `-u` UDP, a WiFi reconnect, and a multi-minute soak all clean.
- OpenOCD (`amebad-openocd-debug`) one-shot reads for any post-mortem (note: the wedged KM4 may go
  tickless/DP-unreachable — use the single-invocation `-c init -c halt -c mdw ... -c shutdown` form,
  not a persistent telnet session).

## Files
- **Import (from TizenRT)**: osdep headers (`osdep_service.h`, `skbuff.h`, `wrapper.h`, `wlan_intf.h`,
  `wireless.h`), WiFi-driver headers (`autoconf.h`, `wifi_constants/structures`, `wlan_bssdef`, the
  `rtw_*` struct headers) → into `component/amebad/osdep/`, `component/amebad/wlan/` (+ `osdep/`),
  `component/amebad/wifi/` / `sdk_includes`.
- **Swap (binary)**: `component/amebad/wlan/lib_wlan.a` ← TizenRT `lib_wlan_fpu.a`;
  `component/amebad/wpa_supplicant/lib_wps.a` ← `lib_wps_fpu.a`.
- **Add**: `component/amebad/app_km4/rom_symbol_crypto.ld`, `component/amebad/wlan/src/amebad_wlan_compat.c`.
- **Edit**: `component/amebad/app_km4/CMakeLists.txt` (cat crypto.ld + `--no-warn-mismatch`),
  `component/amebad/wlan/CMakeLists.txt` (compile compat.c), `usrcfg/rtl8721d_wificfg.c`
  (`wifi_config` alias), `component/freertos-kernel_config/freertos_heap_tlsf.c` (temp harness).
- **Reconcile**: `component/amebad/osdep/freertos_service.c`, `osdep_service.c`, `wlan/src/rtw_opt_skbuf.c`
  (skb struct), and any TU that breaks on the newer headers.
