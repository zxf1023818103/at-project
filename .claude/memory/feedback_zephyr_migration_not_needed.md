---
name: feedback-zephyr-migration-not-needed
description: 用户已评估 nuwa / Zephyr 主线 bflb 后明确表态：本项目自家 SDK driver 抽象重构完之后，迁 Zephyr 不再值得，默认不迁移
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d494c1db-163c-400c-89e9-2e6d40fc1162
---

评估过 Realtek 的 [Ameba-AIoT/nuwa](https://github.com/Ameba-AIoT/nuwa)（Zephyr-based, 覆盖 AmebaD/Dplus/Green2）和 Zephyr 主线对 bouffalo 的 in-tree 支持（bl60x/bl61x/bl70x/bl70xl/bl808 全家）后，用户的决定：**短期内不打算把 AT 项目迁到 Zephyr 路径**。

**Why:** 自家正在做的 SDK driver 抽象/重构（`experiment/driver-only` 这条方向，配合 [[feedback-unified-uart-abstraction]] 偏好的 Uart* 一组内部 #ifdef 风格）已经把"跨平台消 #ifdef、统一 HAL"这部分价值吃下。Zephyr 化剩下能再多拿的好处（mcuboot + TFM 安全启动、统一 lwip/littlefs/lvgl subsystem）在当前优先级下不够支撑迁移成本（要重写所有 Wi-Fi/BT bring-up，flash 分区体系全换，CI 加新工具链）。

**How to apply:** 后续遇到"是不是该上 Zephyr / nuwa / hal_bouffalolab"的讨论，默认按"不迁移、把它们当 reference 情报源"处理 —— nuwa 的 dts/pinctrl/分区方案、bflb 主线的 hci_bflb.c 等可用来对照排查现有原生 SDK 集成问题，但不主动提议"切过去"。仅当用户主动提起，或上下文出现硬需求（要 mcuboot/TFM 的安全启动认证、要 5 个 SoC 共用一份 BT/WiFi/lwip stack）时再翻案。

附差异速查：nuwa 有 WiFi (`drivers/wifi/ameba`) 没 BT；Zephyr 主线 bflb 有 BT (`drivers/bluetooth/hci/hci_bflb.c`) 没 WiFi —— 两边互补但都不全。
