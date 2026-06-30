---
name: reference_bouffalo_bl616_bt_dualmode
description: "BL616/BL618 是双模蓝牙(BR/EDR+BLE+Mesh),对照 RTL8721D 纯 BLE;含 bouffalo_sdk 里启用经典蓝牙的开关"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

**BL616/BL618 是真双模蓝牙**(BR/EDR 经典 + BLE + BLE Mesh 全支持),不是纯 BLE。证据在
`platform/bouffalo_sdk/components/wireless/bluetooth/`:
- 控制器库 `btblecontroller/lib/` 有 **`libbtblecontroller_bl616_ble1m2s1bredr1.a`**(`bredr1`=经典开)
  和 `..._ble1m10s1bredr0.a`(`bredr0`=纯 BLE)两种变体——`bredr1` 证明 silicon 有经典能力。
- host = `blestack/`(**Zephyr Bluetooth host 移植**,所以天然 LE+Mesh+Classic):有 `host/l2cap_br.c`
  (BR/EDR L2CAP)、`src/sbc/`(A2DP 的 SBC 编解码)。
- `btprofile/`(`libbtprofile.a`):`hfp_hf.h`/`rfcomm.h`/`sdp.h` + A2DP/AVRCP(经典 profile)。
- BLE Mesh:`blemesh/`(mesh_models)。

**但 at-project 当前把 BL616 编成纯 BLE**:`project/bouffalo/proj.conf` 选
`CONFIG_BTBLECONTROLLER_LIB=ble1m10s1bredr0`(bredr0)。要启用经典蓝牙:换成 `ble1m2s1bredr1` +
打开 BR/EDR host 配置 + 链 libbtprofile。(BL616CL 用 `m2s1`,文件名没标 bredr 位,要单独查。)

**BL616 的 802.15.4(Zigbee/Thread radio)**:bouffalo_sdk `components/wireless/` 给的是
`lmac154/`(802.15.4 MAC,二进制 liblmac154_bl616.a)+ `thread/openthread/`(**OpenThread**,BSD,开源)
+ `matter/`(Matter,开源),**没有 Zigbee 栈**。要 Zigbee 得自己把栈(如 ZBOSS)移植到 lmac154。
开源大盘:**设备端完整 Zigbee 栈基本没真开源的**——ZBOSS(DSR/Nordic,二进制免费)/EmberZNet(SiLabs)/
Z-Stack(TI)全私有二进制;开源的 zigpy 是 host/网关侧(HA ZHA 用),非 MCU 固件。开源 + 802.15.4 mesh
的务实路线 = OpenThread/Matter(BL616 现成带),不是 Zigbee。

**对照**:RTL8721D(AmebaD)是纯 BLE radio,经典蓝牙物理上没有(实测发 Read Class of Device/Scan
Enable/Voice Setting/Page Timeout 全返回 0x01 Unknown,见 [[project_amebad_ble_dtm]])。所以项目里
要"BLE Mesh + BLE + 经典蓝牙"三合一,**BL616 是现成的那颗**(配 Bouffalo 的 Zephyr 系 blestack);
AmebaD 只能 BLE + Mesh。
