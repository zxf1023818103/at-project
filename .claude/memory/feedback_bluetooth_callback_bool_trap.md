---
name: bluetooth-callback-bool-trap
description: "BluetoothTask 的 sendCallback<bool> 把 callback 返回 false 当作\"退出任务\"信号——不能用来传递业务失败 bool"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 02da2303-28fa-40fe-bd97-be689268063b
---

`BluetoothTask::exit()` 通过 `sendCallback<bool>([](){ return false; })` 来杀 BT routine：

```cpp
// AmebaBluetooth::routine()
(*callbackPtr)->startSync();
success = (*callbackPtr)->success();
if (!success) {
    break;  // ← 整个 BT 任务退出
}
```

`CallbackTask<bool>::success()` 直接返回 callback 的 bool 结果，所以 **任何** 用 `sendCallback<bool>` 包装并返回 `false` 的内部 op 都会顺带杀掉 BT routine。

**Why:** 2026-05-23 review 整改时把 `BluetoothTask::setDeviceName` 从 `void` 改成 `bool`（让调用方能看到失败），结果实测 Green2 上 `gaps_set_parameter(DEVICE_NAME)` 在 adv 已经在跑时返回非 SUCCESS，整个 testBluetoothTask1 就在第二条 log 之后静默死掉。修复见 commit 8f6ac2e。

**How to apply:**
- 在 BluetoothTask 里加 BT op 时，**不要**把 "业务失败" 通过 `sendCallback<bool>` 的 false 返回上来——会触发 exit。
- 要么用 `sendCallback<void>` + 内部 `logFormat(WARN, ...)` 暴露失败；
- 要么用 `sendCallback<shared_ptr<X>>`（CallbackTask 走 smart-pointer 特化，`success() = m_result != nullptr`，不和 exit 信号撞）；
- 历史上 `startAdvertising` / `updateAdvertisingData` / `stopAdvertising` 已经用 `sendCallback<bool>` 并返回真实成功 bool——它们在实际硬件上总是返回 true 才没炸；如果以后这些 op 也开始失败，需要一并改设计。

相关：[[project_src_wiring]] [[ameba-openocd-flash]]
