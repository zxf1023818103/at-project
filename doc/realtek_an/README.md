# Realtek Ameba 文档（NDA / 厂家机密 — 仅本地查阅）

本目录放 Realtek 官方文档（数据手册、应用笔记、MP 量产指南、pinmux 表），供本地查阅。
**全部归 Realtek 版权、受 NDA 或"Confidential"约束** —— 已在仓库 `.gitignore` 里排除整目录
的二进制文档（`*.pdf *.chm *.xlsx *.xls *.doc*`）：**不进 git、不要推到远端**。本 README 可提交。

> 提取出来、已核实的事实（eFuse 地图 / 算法 / 保护位 / 厂家保管规则）整理在
> [`../amebad_keys.md`](../amebad_keys.md)、[`../amebad_tfm_storage.md`](../amebad_tfm_storage.md) —— 那两份可公开。
> 开源化 / 外发本仓前，确认本目录及其内容已清除。

文件名已去掉下载站的 `(NNNNN)` 后缀。本项目主目标芯片是 **RTL8721D（AmebaD）**，
次目标 **RTL8711Dx（AmebaDplus，`project/ameba`）**，故两者文档都留。

## 安全 / 启动（NDA — 核心机密）

| 文件 | 内容 | 本仓引用 |
|---|---|---|
| `AN0411 Ameba-D Secure eFuse v01-NDA.pdf` | eFuse 物理/逻辑地图、各密钥区、保护配置位 | [amebad_keys.md](../amebad_keys.md) |
| `AN0410 Ameba-D Secure Boot v3.0-NDA.pdf` | Secure Boot（Ed25519 签名校验）、Secure Image2、三方烧录流程 | [amebad_keys.md](../amebad_keys.md) §二 |
| `AN0405 Ameba-D RSIP v02-NDA.pdf` | RSIP（AES-128 OTF 镜像加密）、No-RSIP mask、key/IV、烧录 | [amebad_keys.md](../amebad_keys.md) §一 + [amebad_tfm_storage.md](../amebad_tfm_storage.md) §9 |
| `AmebaD Application Note-Secure Boot and Image2_v02.pdf` | Secure Boot + Image2（与 AN0410 一致，enable 位 `0x1C1[5]`/`0x1C1[7]`） | 同 AN0410 |
| `Realtek ImageTool Download User Manual v2.0-NDA.pdf` | ImageTool 量产烧录手册（NDA） | [amebad-imagetool-flash 技能](../../.claude/skills/amebad-imagetool-flash) |

## 芯片手册 / pinmux（厂家机密）

| 文件 | 芯片 | 内容 |
|---|---|---|
| `DS1001 RTL8721Dx Datasheet v2.0.pdf` | RTL8721D | **主芯片数据手册**（寄存器/外设/电气） |
| `DS1050 RTL8711Dx Datasheet v1.0.pdf` | RTL8711Dx | AmebaDplus 数据手册 |
| `UM1002 RTL8721Dx pin_mux v1.0.xlsx` | RTL8721D | pinmux 表 |
| `UM0402 RTL872xD pinmux R61.xlsx` | RTL872xD | pinmux 表（R61，更全） |
| `PM1050 RTL8711Dx pin_mux v1.0.xlsx` | RTL8711Dx | pinmux 表 |

## MP 量产 / 工具 / 测试

| 文件 | 内容 |
|---|---|
| `Realtek Ameba MP ImageTool Application Note v3.0.pdf` | MP ImageTool 应用笔记 |
| `AN0004 Realtek low-power Wi-Fi MP User Guide.pdf` | 低功耗 Wi-Fi 量产校准指南 |
| `Realtek BT RTL8721D-RTL8720C Non-Signaling MP Flow.pdf` | 蓝牙非信令量产流程 |
| `Realtek Ameba MP HCI Vendor Command v1.0-Draft.pdf` | BT HCI vendor 命令（量产/DTM 相关） |
| `IoT Wi-Fi Channel Plan and Power Setup Guideline R01.pdf` | 信道计划 / 功率设置（呼应 channel-plan reboot 坑） |
| `UM0205 Ameba MbedTLS Version Management UG.pdf` | Ameba mbedTLS 版本管理（呼应 TF-M 内 mbedcrypto） |
| `AmebaD USB Quick Start V1.2.pdf` | AmebaD USB 快速上手 |
| `UM0202 Ameba BT Throughput Test Example UM.pdf` | BT 吞吐测试示例用户手册 |
| `AN0412 Ameba-D Throughput Test Report.pdf` | AmebaD Wi-Fi 吞吐测试报告 |
| `AN0400 Ameba-D Application Note v12.pdf` | **总应用笔记**（build/IPC/flash/power/wifi 全覆盖）。关键章节：§18 Flash Operation（SPIC auto/user 模式互斥 + `FLASH_Write_Lock` 跨核保护，Fig 18-1 IPC 睡眠握手）、§12 IPC（`ipc_init_config[]` 两核 + `ipc_table_init`）、§15 Power Save、§17 ipccfg/sleepcfg —— TF-M 安全存储跨核 flash 仲裁的背景依据，见 `project_amebad_aws_iot` 记忆 |
| `TR1060 RTL8711Dx Cable Link RvR Test Report.pdf` | RTL8711Dx RvR 测试报告 |
| `AmebaD-LT 路由器兼容性测试报告.pdf` | AmebaD 路由器兼容性测试（呼应 2.4G 拥塞坑） |

> 来源：本地 `D:\Ameba`。off-target 芯片文档（RTL8730E / RTL8720E / RTL8713EC）未收。
