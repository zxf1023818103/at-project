# AmebaGreen2 / RTL8711F

仿照 `project/amebadplus` / `project/amebad`：独立 CMake/Ninja 镜像工程放在项目目录，硬件文件按归属放在 `component/amebagreen2`，FreeRTOS、TCP、TLS、LittleFS、NimBLE、AWS 和 TF-M 复用共享组件。原来的 SDK 包装工程（顶层 CMake/Kconfig/prj.conf、env 脚本和 console shim）已移除。

## 工程对应

| Green2 目录 | 职责 | Dplus 对应 |
|---|---|---|
| `bootloader_km4tz` | AP 启动、双槽验证、可选 SPE 启动 | `bootloader_km4` |
| `bootloader_km4ns` | NP 启动对象，编入 NP 应用镜像 | `bootloader_km0*` |
| `app_km4ns` | 无线 NP、IPC、共存 | `app_km0` |
| `app_km4tz` | AP、CLI 和共享应用 | `app_km4` |
| `app_km4tz_secure` | TF-M SPE / ITS / PS | `app_km4_secure` |

Green2 是两个 Armv8.1-M 核心；不沿用 Dplus 的 KM0 名称或镜像填充规则。每个核心的 image2 包含连续的 XIP/SRAM/PSRAM 三段。TF-M 尾部随 APP_ALL 一起升级，使用独立 XIP 映射；SG 入口使用可配置的 `0x0c000800` 窗口，SPE 数据在安全 SRAM。

## 构建和烧录

依赖 CMake 3.24+、Ninja、Python 3，以及 ASDK 12.3.1-4568 的 soft-float/newlib 工具链。默认查找 `toolchain/asdk-12.3.1-4568/linux/newlib` 或 `/opt/rtk-toolchain/asdk-12.3.1-4568/linux/newlib`，可通过 `AMEBAGREEN2_TOOLCHAIN_ROOT` 指定。

```sh
make -C project/amebagreen2 deps
make -C project/amebagreen2
make -C project/amebagreen2 flash PORT=COM15 BOOT_PORT=COM19
make -C project/amebagreen2 cli_test PORT=COM15 BOOT_PORT=COM19
make -C project/amebagreen2 test
```

COM15 同时用于下载和控制台，速率 1500000。COM19 的 RTS 控制复位、DTR 控制下载引脚。`flash.ps1` 使用本目录的 `flash_uart.py` 和提取的 Green2 Flash 下载器，自动下载、逐字节读回校验、复位并检查应用启动。Windows Python 需要 `pyserial`；`make deps` 安装的工具依赖位于忽略的 `build/amebagreen2/python-deps`。

默认只写 `firmware.bin` 中的 boot 和 OTA1，保留系统/校准区、OTA2、LittleFS 和安全存储。替换 LittleFS 必须显式传入 `flash.ps1 -Lfs <960KiB镜像>`。`-Backup <新文件>` 可先备份整片 4 MiB，拒绝覆盖已有备份；`-VerifyOnly` 只校验。

```powershell
.\project\amebagreen2\flash.ps1 -Port COM15 -BootPort COM19 -Image .\build\amebagreen2\firmware.bin -Backup C:\temp\green2-original.bin
.\project\amebagreen2\serial_cmd.ps1 -Port COM15 -BootPort COM19 -Reset -Cmds 'help','heap'
```

`BUILD` 可指定独立输出目录。`make image` 还会生成包含 `project/resource` 内容的 `Image_All.bin`，但默认烧录流程使用保留配置的 `firmware.bin`。`ota_all.bin` 是 HTTP/HTTPS 和 mcumgr BLE 共用的升级文件。

`AWS=ON` 或 `AWSOTA=ON` 提供 `ota_http <http或https完整URL>` 控制台命令。HTTPS 校验服务器证书；内置公共根证书之外的 CA 可通过 `crt_bundle addca` 配置（该诊断命令需要 `CRYPTO_SELFTESTS=ON`）。

`MCUMGR=ON` 时，可在安装了 `bleak` 的 Windows Python 中执行以下命令，BLE 名称末尾取板卡蓝牙 MAC 的后三字节：

```powershell
python .\project\amebagreen2\mcumgr_upload.py --name amebagreen2_5906ff --image .\build\amebagreen2\ota_all.bin
```

传输结束的断连可能是设备重启；需要核对串口中的目标槽启动及应用自测。HTTP/HTTPS 和 BLE 均在完整下载与校验后才提交新槽，TF-M SPE 随镜像一同更新。

## 可选功能

| 选项 | 行为 |
|---|---|
| `NIMBLE=ON` / `HCI=ON` | BLE 主机 / 原始 H4 控制器桥；两者互斥 |
| `NAN=ON` | 单 Green2 FreeRTOS NAN、PASN 配对及 TCP 测速服务 |
| `NAN_BLE_OOB=ON` | 通过加密 BLE GATT 获取临时 NAN PIN，自动启用 NAN 和 NimBLE |
| `NAN_USB=ON` | Green2 无线侧 USB 桥接，由 BL618DG 运行共享 NAN 主机协议栈 |
| `BLE_NAN_WAKE=ON` | BLE 连接唤醒 NAN，传输后自动返回 PG；需 `NAN=ON NIMBLE=ON` |
| `NAN_DEEP_SLEEP=ON` | NAN 传输后进入整片 DSLP；需 `NAN=ON NIMBLE=OFF` |
| `BLE_POWER_TEST=ON` | 仅 BLE 广播的功耗测试；需 `NIMBLE=ON`，停用 NAN、TF-M、USB benchmark |
| `MCUMGR=ON` | SMP BLE 文件/镜像服务，自动启用 NimBLE |
| `AWS=ON` / `AWSOTA=ON` | 遥测、配置、HTTP/HTTPS OTA / 完整 AWS Jobs OTA Agent；后者自动启用 AWS |
| `TLS_BACKEND=picotls\|mbedtls\|fallback` | TLS 后端；默认 picotls |
| `CRT_BUNDLE=ON` | 内置公共 CA；AWS 配置默认开启，也接受 `ca/` 的本地根证书 |
| `WPS=ON` / `EAP=ON` | WPS PBC / EAP-TLS 配置服务 |
| `TFM=ON` / `STORAGE=ON` | 安全域平台网关 / ITS、PS；STORAGE 自动启用 TFM |
| `HWGCM=ON` / `ROMKAT=ON` | 硬件 GCM 加速 / ROM 与上层加密已知答案测试 |
| `PKCS11=ON`、`BENCH=ON`、`CRYPTO_SELFTESTS=ON` | PKCS#11 自测、加密基准、诊断命令 |
| `VFSTEST=ON` / `CPPTEST=ON` | POSIX 文件/套接字 / C++ 文件流和 filesystem 自测 |
| `QUIC=ON QUIC_SERVER=x.x.x.x` | 共享 QUIC 吞吐测试 |
| `IPERF=ON`、`CARES=ON`、`MINIMAL=ON` | TCP 性能命令、c-ares DNS、精简配置 |
| `TLS_HEAP_TRACE=ON` | picotls 堆诊断 |
| `SBOOT=ON` / `RSIP=ON` | 主机 Ed25519 签名 / RSIP XTS 加密，要求已有板卡匹配密钥 |
| `EFUSE_DEBUG=ON` | 显式开启 OTP 调试命令；默认关闭 |

这些功能可按配置分别构建，不表示任意组合都能放进一个 OTA 槽。打包器检查 boot 和应用大小并拒绝越界。

`NAN_BLE_OOB` 与 `BLE_NAN_WAKE`、`BLE_POWER_TEST`、`NAN_DEEP_SLEEP`、`NAN_USB`、`HCI`、`MCUMGR` 互斥。临时 PIN 配对流程及保存配对说明见 [NAN 组件文档](../../component/amebagreen2/nan/README.md)。

TF-M Python 依赖通过 `make tfm_deps` 安装；CMSIS/QCBOR 由 TF-M 按固定版本获取，已有本地依赖缓存时可复用。SPE 使用私有的 mbedTLS 构建副本，不改动共享子模块。

```sh
make -C project/amebagreen2 BUILD="$PWD/build/amebagreen2-complete" \
  STORAGE=ON AWS=ON MCUMGR=ON WPS=ON EAP=ON \
  CRYPTO_SELFTESTS=ON HWGCM=ON ROMKAT=ON \
  FW_VERSION=green2-sec-A APP_BUILD=3
```

## Flash 与安全选项

| 物理偏移 | 用途 |
|---|---|
| `0x000000..0x00dfff` | Boot |
| `0x00e000..0x00ffff` | 系统与射频校准，烧录保留 |
| `0x010000..0x17ffff` | OTA1，1472 KiB |
| `0x180000..0x18ffff` | OTA2 boot 保留 |
| `0x190000..0x2fffff` | OTA2，1472 KiB |
| `0x300000..0x3effff` | LittleFS，960 KiB |
| `0x3f0000..0x3fffff` | TF-M ITS/PS、开发用 HUK 和计数器，64 KiB |

固件启动时解除 NOR 写保护，包括旧固件遗留的 BP/CMP 设置，不再自动锁定底部 3 MiB。非 TF-M 模式下，已解锁时不重复写状态寄存器；可用 `flash status` 查询，`flash lock` 仍可手动设置保护。TF-M 通过受检查网关执行擦写，拒绝启动区、当前槽及由 NS 直接访问的安全存储请求。当前采用 TF-M 隔离级别 1；HUK 由 TRNG 生成并保存在安全 Flash，属于开发用存储后端，没有把普通 Flash 计数器当作硬件防回滚保证。

`dsleep rtc <秒>` / `dsleep uart rtc <秒>` 使用 AP 保留式 SleepCG，NP 保持无线、IPC 和看门狗任务运行；不进入整片断电。FreeRTOS 的下一次调度期限驱动 PMC 唤醒定时器，TF-M 启动时设置的安全权限在休眠前后保持不变。

AP 的 PMC 日志入口过滤 `RTK_LOG_ALWAYS` 等级的两个固定字符串 `APCG\n` / `APCW\n`，避免每次浅睡眠切换都打印；tickless、其他诊断日志和 NAN 每 10 秒的 `[NAN-PM]` 统计保留。

### BLE 广播功耗测试

```sh
make -C project/amebagreen2 BUILD="$PWD/build/green2-ble-power" \
  NIMBLE=ON BLE_POWER_TEST=ON MINIMAL=ON IPERF=OFF NAN=OFF TFM=OFF image
```

此配置不启动保存凭据的 Wi-Fi 连接服务或 TCP 网络接口。WLAN 仅初始化以完成蓝牙射频校准，然后按现有 `RTW_IPS_WIFI_OFF` 策略进入 IPS。上电默认发送 `Green2 BLE Test A` 非连接广播：22 字节 AD 数据、37/38/39 三个信道、125 ms 间隔；控制器设置及回读发射功率为 8 dBm。

```text
blepower status
blepower adv non 125
blepower adv non 1000
blepower adv conn 125
blepower adv conn 1000
blepower sleep pg 68
blepower stop
```

`sleep cg|pg <1..300秒>` 临时释放 AP 的 `PMU_OS` 锁。此非 TF-M 测试配置同时允许 NP 休眠，保留两个核的调度期限和看门狗，单次空闲最长 500 ms；超时后恢复 AP 原有锁和睡眠类型。启动命令后等待约 1 秒再采集 60 秒电流，采样期内不要连接 BLE。`connections` 必须为 0，状态 `adv=1 rc=0 tx_dbm=8`，并检查 `APPG/NPPG` 日志和恢复后的广播。可连接广播仍只测广播状态，意外连接会计数并主动断开，该段数据应作废。

对照 TR1020 R02 第 9 页时还需记录供电、室温、芯片完整型号和仪器校准状态。报告的 25°C、3.3 V、PG 数据是特定 RTL8721F 型号的典型 IC 电流；整板结果包含当前固件的周期唤醒、日志及板级负载，不能直接用于判断 IC 是否超出规格。

测试休眠广播和连接唤醒时，先选择可连接广播，再进入允许连接的定时测试窗口：

```text
blepower adv conn 125
blepower wake pg 300
```

主机可扫描 `Green2 BLE Test A` 并连接。PG 前通过 UART RTS 延迟控制器的 HCI 输出，`BT_WAKE_HOST` 唤醒 AP 后再恢复串口接收；连接期间持有 `PMU_BT_HOST`，断连并完成重新广播设置后释放。GATT 样例特征 `59462f12-9543-9999-12c8-58b459a2712e` 支持单字节写入和回读。`BLEWAKE` 日志记录硬件唤醒源、连接期间的休眠计数及唤醒锁。窗口结束后恢复原有 OS 锁；普通 `sleep` 功耗测试仍会拒绝连接。

### BLE 连接唤醒后启动 NAN

```sh
make -C project/amebagreen2 BUILD="$PWD/build/green2-ble-nan-wake" \
  NIMBLE=ON NAN=ON BLE_NAN_WAKE=ON NAN_USB=OFF MINIMAL=ON IPERF=OFF TFM=OFF image
```

`BLE_NAN_WAKE` 默认关闭，与 `BLE_POWER_TEST` 互斥。启用后，上电首次待机无需发送休眠命令：

1. 上电完成 RF 校准和 BLE 初始化，广播 `Green2 NAN Wake`，125 ms 间隔、8 dBm；AP/NP 自动进入 PG，WLAN 使用 IPS。此时不启动 NAN、IP 或 TCP 服务。
2. BLE 连接通过 `BT_WAKE_HOST` 唤醒 AP，立即持有 OS 唤醒锁，通知现有 NAN 任务初始化接口、PASN、发布和 TCP 8000 服务。
3. 日志出现 `[BLENAN] NAN active rc=0` 后，App 可连接 `Green2 FreeRTOS`，沿用原有 NAN 配对；首次配对 PIN 为 `123456`。BLE 断开后 NAN 继续工作，完成传输后按下面的空闲规则自动待机；重启也重新进入 BLE 广播待机。

BLE 连接本身就是启动触发，不要求写 GATT。广播扫描响应包含现有服务 UUID `59462f12-9543-9999-12c8-58b459a2712d`，样例特征 `59462f12-9543-9999-12c8-58b459a2712e` 可做单字节读写验证。App 若要自行唤醒设备，需要先完成这一步 BLE 连接，再执行 NAN 发现/连接。

BLE 唤醒后可用 `blenan status` 查看启动/活动状态、连接数、休眠计数及唤醒锁；`nan status` 查看真实 NAN 和网络状态。PG 期间 LOGUART 接收时钟会被门控，不依赖串口命令唤醒。待机期间 `nan start/stop` 不会绕过 BLE 触发。此模式仅支持非 TF-M 的单 Green2 NAN，保留 500 ms 看门狗/调度期限和串口轮询。NAN 启动后恢复中断控制台，保持 AP 活跃以等待 App 连接。

一次上传或下载成功后，自动开始 **5 秒空闲计时**。下一条命令到达便取消旧计时，当前传输完成后重新计时，因此连续上传、下载之间不会立即休眠。空闲到期且 TCP 发送缓冲清空后关闭客户端连接，由 NAN owner task 执行同一条暂停/PG 路径，日志为 `[NAN] auto standby queued after transfer ...`。BLE 仍连接时等待其断开后再暂停 NAN；仅唤醒但还未传输、PING 或失败的传输不会单独触发自动待机。下一轮传输需要先通过 BLE 唤醒。5 秒是应用空闲策略，之后还存在无线接口停止、IPS 和电流降稳的过渡时间。

NAN 开启后，再次进入仅 BLE 广播待机：

```text
blenan sleep
```

此命令要求当前 BLE 已断开、NAN 正在运行。它会结束当前 NAN 数据连接，在 NAN owner task 中停止 NAN 并关闭无线接口，使 WLAN 可以进入 IPS，随后恢复 BLE 广播和 AP/NP PG。主机 IP 栈、TCP 监听任务、PASN 缓存对象及 RAM 中的配对身份保留；已建立会话的授权和临时密钥清除，后续连接必须重新认证。

下一次连接 `Green2 NAN Wake` 后，日志出现 `[BLENAN] NAN resumed rc=0` 即可重新用 App 连接 NAN。恢复时不重复 Wi-Fi/IPC 完整启动、P-256 启动自检、PASN 初始化和配对文件读取，也不重复创建 IP/TCP 任务；仍需重新打开 NAN 无线接口、建立无线调度并发布服务。只执行 NAN STOP、保留无线接口会阻止 IPS，因此不能完全跳过无线接口初始化。`nan status` 同时输出 `host_inits`、`radio_inits`、`pauses`、`resumes`，用于核对保留状态的恢复次数。BLE 断开本身不会触发休眠；自动计时或 `blenan sleep` 都会清除本轮状态，避免恢复后被旧计时立即休眠。

### NAN 传输后进入 SoC 深度休眠

```sh
make -C project/amebagreen2 BUILD="$PWD/build/green2-nan-deep" \
  NAN=ON NAN_DEEP_SLEEP=ON NAN_DEEP_SLEEP_MS=30000 NIMBLE=OFF \
  BLE_NAN_WAKE=OFF NAN_USB=OFF MINIMAL=ON IPERF=OFF TFM=OFF image
```

`NAN_DEEP_SLEEP` 默认关闭，与 BLE 唤醒/广播测试模式互斥。上电或 DSLP 唤醒后直接初始化 NAN；沿用传输后 5 秒空闲检测，停止无线接口后释放 deep wakelock，由 AP tickless 通知 NP 执行 `SOCPS_DeepSleep_RAM()`。该模式不启动 BLE，唤醒源为 AON 定时器；关闭定时器时，可通过断电再上电启动。

DSLP 不保留运行中的主机协议栈。每次唤醒都会经过 ROM、bootloader 和应用启动，重新从 LittleFS 加载配对；用 `[NAN-DEEP] boot reason=00000002 ... source=dslp` 区分深睡唤醒与普通复位/上电。`NAN_DEEP_SLEEP_MS` 是每次传输后深睡时长，默认 30000 ms，允许 0～3600000；0 表示不启用 AON 定时唤醒。重新启动后若尚未传输，NAN 保持开启，等待 App。

手动进入深睡可使用以下命令；参数只作用于当前这一轮，重启后恢复编译设置：

```text
nan sleep 10
nan sleep 0
```

第一条按 SDK 的标称 100 kHz 设置 10 秒 AON 唤醒，第二条关闭定时唤醒。该 RC 定时器并非精确定时源：COM43 实测 10 秒设置对应约 14.24 秒深睡，唤醒仍正常；当前保留 SDK 的计数方法，实际时长以测量为准。未修改 eFuse 或振荡器 trim。

`SBOOT`、`RSIP` 和 `EFUSE_DEBUG` 默认关闭。密钥文件默认为本地忽略的 `keys/com15.json`，可用 `SECURITY_KEYS` 覆盖；缺失时打包器拒绝自动替换设备密钥。Green2 密钥字段为 `root`、`app`、`rsip_data`、`rsip_tweak`、`rsip_app_data`、`rsip_app_tweak`（各 32 字节 hex），以及 `boot_iv`、`app_iv`（各 8 字节 hex）。启动 RSIP 使用 OTP 0x2c0/0x2e0，应用与 SPE 使用独立的 0x200/0x220 组。

`make flashloader` 用同一 root 密钥签名 RAM 下载器。主机签名和加密不会写 eFuse；只有已配置匹配熔丝的板卡才应烧录对应加密镜像。COM15 本次适配没有烧写 eFuse，也没有复用 COM42 的设备密钥。

来源见 [组件说明](../../component/amebagreen2/README.md) 和 [UPSTREAM.json](../../component/amebagreen2/UPSTREAM.json)，实测覆盖与限制见 [VALIDATION.md](VALIDATION.md)。
