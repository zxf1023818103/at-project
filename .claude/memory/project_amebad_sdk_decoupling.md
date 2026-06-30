---
name: amebad-sdk-decoupling-status
description: AmebaD driver-only build — all out-of-tree .c are now vendored into component/amebad; what SDK coupling remains (headers + prebuilt .a) for full "脱离原厂 SDK"
metadata:
  type: project
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

Goal: build `component/amebad` (driver-only RTL8721D) **脱离原厂 SDK**
(`platform/ameba-rtos-d`). As of 2026-06-03, **no SDK `.c`/`.S` source is compiled
in-place anymore** — every component compiles only files under `component/amebad/`.

**Vendored this round** (out of `platform/ameba-rtos-d/...` into the component):
- `osdep_km4/src/` — freertos_service.c, device_lock.c, osdep_service.c, psram_reserve.c
- `ftl_km4/src/` — ftl.c, flash_api.c, + ftl's private headers ftl.h / ftl_int.h
- `at_cmd_km4/src/` — log_service.c (+ the authored at_cmd_stubs.c moved here)
- `monitor/` (shared `ram/` + `rom/`, compiled per-ABI by `monitor_km0` + `monitor_km4`,
  mirroring `freertos_port/`) — monitor_lp/hp.c, shell_ram.c, rtl_trace.c,
  low_level_io.c, rom/shell_rom.c, rom/monitor_rom.c
Earlier rounds already vendored: bootloaders + apps (`*/src/`), ram_common/ram_lp/
ram_hp, usrcfg_*, fwlib_headers, common_ld_script, freertos_port (glue half).
Convention: per-component vendored sources live in `<component>/src/`; sources
shared by km0+km4 live in a shared sibling folder (freertos_port/, monitor/).

**Remaining SDK coupling:**
1. **Headers** ✅ DONE — fully vendored into `component/amebad/sdk_headers/` (85
   headers, SDK `component/...` layout preserved so cross-includes resolve);
   `sdk_includes` + `ftl`'s mbed `_base` repointed there. The build now references
   **0** `platform/ameba-rtos-d` headers (verified via the compiler `.d` files /
   `grep -rhoE 'platform/ameba-rtos-d/[^ "]*\.h' build/`). Method that worked:
   build → for each missing header find its SDK dir → copy that include-root's
   whole `*.h` (cherry-picking from `.d` MISSED transitive headers like
   psram_reserve.h / log_service.h). Earlier `at_cmd` lwip → `lwip_amebad` submodule
   + vendored `lwip/config/lwipopts.h`; SDK mbedtls header → shared mbedtls/include.
2. **Prebuilt vendor `.a` blobs** ✅ DONE — `lib_pmc_lp.a` / `lib_wifi_fw.a` /
   `lib_pmc_hp.a` copied into `component/amebad/prebuilt/`; app_km0/km4
   IMPORTED_LOCATION repointed there.

**MILESTONE (2026-06-04): the AmebaD driver-only build references ZERO
`platform/ameba-rtos-d` paths** — verified `grep -rn platform/ameba-rtos-d
component/amebad --include=CMakeLists.txt` is empty and the build products carry
no platform/ header/lib refs. Sources, headers (sdk_headers/), prebuilt .a
(prebuilt/), FreeRTOS/lwip/mbedtls (shared thirdparty) are all in component/.
The SDK submodule no longer needs to be checked out to build amebad — it could
be removed entirely (like freertos_amebad was), pending a check that nothing else
(docs / resync tooling) still wants it.

**Shared third-party stack (goal: all platforms one copy, like FreeRTOS).** Plan:
`component/thirdparty/` = clean upstream lib + per-platform `<plat>_*_port`/config
adapter (Bouffalo's `bflb_*_port` is the template). Progress:
- **FreeRTOS** ✅ shared upstream V10.6.2.
- **mbedtls** ✅ first slice: `component/amebad/mbedtls/` builds the shared
  `component/thirdparty/mbedtls` **v3.6.4** for KM4 (mbedcrypto/mbedtls/mbedx509 .a
  verified). Config = Bouffalo's `config-tls-generic.h` verbatim (Kconfig-gated,
  platform-clean) minus `CONFIG_MBEDTLS_*_USE_HW` → software crypto; own mem header.
  EXCLUDE_FROM_ALL (no consumer yet). **Remaining**: entropy port (Realtek TRNG
  `mbedtls_hardware_poll`), `net_sockets` (needs lwip), optional HW-accel
  (`rtl8721d_crypto`) — only needed at exe-link. SDK mbedtls-2.4.0/3.x retire then.
- **lwip** ✅ **完全移除(2026-06-06,commit 145b725+bdb4f19)**:AmebaD IP 栈是
  FreeRTOS-Plus-TCP,lwip 不再需要。删了 `component/thirdparty/lwip_amebad` 子模块、
  lwipcore/amebad_lwip 库构建、lwipopts.h/lwippools.h、wifi/lwip_netconf.c。**净残留 =
  唯一一个 lwip-free vendored shim** `component/amebad/lwip/config/lwip_netconf.h`
  (~110 行:最小 `struct netif` + NET_IF_NUM + LwIP_*/netif_* 声明)——这是闭源
  lib_wlan.a 的硬 ABI(它直接访问 `xnetif[]`,字段 hwaddr@+50 / hwaddr_len@+56、
  72B stride;三条 _Static_assert 把布局漂移变编译失败)。LwIP_GetMAC 是 connect 期
  mgmt-frame 过滤的 load-bearing。详见 [[amebad-freertos-tcp-bringup]]。坑:wifi_conf.c
  的 lwip 重用函数(AP dhcps/静态IP/keepalive/ARP)靠 gc-section 剔;struct eth_drv_sg
  原来藏在 lwip 的 ethernetif.h 里(删 lwip 后要在 lwip_intf.h 自定义);at_cmd 的
  透传 socket 头是 ATVER_2-only(CONFIG_EXAMPLE_UART_ATCMD=0 故不编)。`amebad_lwip_headers`
  INTERFACE 目标沿用旧名但现仅暴露这个 shim。`component/thirdparty/lwip`(博流 2.1.2)
  是另一个无关子模块,留着给别的平台。

Tooling: the cross-core assembler moved `component/amebad/build_image_all.py` →
**`component/amebad/image_tool/build_image_all.py`** (REPO path bumped to three up).
FreeRTOS port/upgrade gotchas in [[amebad-freertos-v1062-port-selection]];
boot-rodata trap in [[amebad-boot-rodata-xip-trap]].
