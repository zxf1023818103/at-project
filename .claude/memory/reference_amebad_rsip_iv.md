---
name: reference-amebad-rsip-iv
description: AmebaD RSIP 加密 IV — Realtek FAE 私发，stage 2 imagetool wrap 用；at-project 是 private repo 故可入 git
metadata: 
  node_type: memory
  type: reference
  originSessionId: d494c1db-163c-400c-89e9-2e6d40fc1162
---

AmebaD（RTL8721D）SDK imagetool.sh 在 `RSIP_ENABLE=1` 时调
`EncTool rsip <bin> <bin-en> <virtual_addr> $RSIP_KEY $RSIP_IV` 加密 4
个 image（km0_boot_all、km4_boot_all、km0_image2_all、km4_image2_all）。

Realtek FAE（2026-05）私发的 RSIP IV（前 12 字节由 FAE 提供，后 4 字节补 0）：

```
IV = 90 ac d3 4e c7 0a 53 fa 6e ba 8c a4 00 00 00 00
```

ImageTool 文档（Realtek confidential file）红框明确写
"User should consult Realtek for RSIP IV, because it is confidential for
security reason." —— 但 **at-project 是 private repo**（用户 2026-05
明确确认），所以 IV/KEY 可以直接作为 cmake option 默认值入 repo，不
需要走环境变量 / gitignored config 这些 secret-管理路径。

KEY 的性质（重要校正）：文档红框**只对 IV** 写 confidential，KEY
没标 — KEY 是**用户自定的 128-bit**，本地生成即可（`openssl rand
-hex 16`）。Realtek 这个加密模型：IV 全局固定（ROM 解密只认这一个
nonce），KEY 用户私有（每个项目/产品自己定）。所以 KEY 不用 FAE
提供。

Stage 2（axf → km{0,4}_boot_all.bin pipeline 含 imagetool wrap）落地
时的设计：
- `AMEBAD_RSIP_ENABLE` BOOL CACHE，默认 OFF（跟 SDK security_config.sh
  一致；ON 时走 RSIP 加密链）
- `AMEBAD_RSIP_IV` STRING CACHE，默认 = 上面 IV 16 字节 hex（FAE 给的
  Realtek 固定值；at-project private repo 故可入 git）
- `AMEBAD_RSIP_KEY` STRING CACHE，默认 = 项目随机生成的 128-bit hex
  （自己 `openssl rand -hex 16` 拍一个，作为本项目固定 KEY 入 repo；
  量产前可以换、不同产品线可以各开 option）
- 都放 [component/amebad/sdk_includes/CMakeLists.txt](../../-home-zxf-at-project/component/amebad/sdk_includes/CMakeLists.txt)
  或新开 [component/amebad/security/CMakeLists.txt]，跟 OTA /
  RSIP_MASK_ENTRY0 option 同一惯例
- **若日后 at-project 转 public**：立刻把 IV/KEY 默认值清空 + 改成
  cmake -D 必填，or 切到 gitignored override file

关联：
- [[reference-amebad-flash-layout]] —— 4 个加密目标的 virtual address
  (km0_boot 0x08000000, km4_boot 0x08004000, km0_image2 0x0c000000,
  km4_image2 0x0e000000) 已经在 SDK [imagetool.sh] hardcode
- [[amebad-image-build]] —— doc/amebad_image_build.md 提过 imagetool.sh
  作 stage 2 后处理
