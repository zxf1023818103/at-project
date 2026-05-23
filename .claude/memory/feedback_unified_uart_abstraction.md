---
name: feedback-unified-uart-abstraction
description: src/ 下跨平台 HAL 抽象偏好用一套统一类名 + 内部
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 42f3082b-8468-40e9-8522-e5e8ab5f779c
---

at-project/src/ 下涉及多平台 HAL（Bouffalo / Ameba D / Ameba 新树）的封装类，倾向**一套统一类名**（如 `Uart*`）内部用 `#ifdef BL602|... / CONFIG_PLATFORM_8721D|...` 分支切底层 SDK 调用；**不要**为每个平台造一套并列类（如 `BflbUart*` + `AmebaUart*`）。

**Why:** 用户在 BflbUart* 已经存在的前提下，看到我打算另起一套 AmebaUart* 与之并列，主动要求合并成 Uart*。维持单一抽象的好处：构造签名只有一种、Tests/AppTask 不用为每平台再写一份 `#ifdef` 块、对外不暴露底层 SDK 命名（解耦 Bouffalo 品牌词）。

**How to apply:** 新增其它 HAL 类（GPIO、SPI、I2C 等）也走同样模式：先看 src/ 有没有现成的 `Bflb*` / `Ameba*`，有的话先把它改名成无平台前缀的统一类，再在同一组文件里加另一平台的实现；不要为新平台另起 `AmebaXxx*` 与 `BflbXxx*` 并立。设备句柄统一用 typedef（`using UartDevice = ...`）按 `#ifdef` 桥接 SDK 原生类型（`bflb_device_s` / `serial_t`），让上层调用代码完全平台无关。
