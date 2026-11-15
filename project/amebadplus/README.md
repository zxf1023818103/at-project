# AmebaDplus / RTL8711Dx

与 `project/amebad` 一样，镜像入口、配置和链接脚本位于项目目录，硬件适配位于 `component/amebadplus`，中间件复用 `component/*_config`。SDK `platform/ameba-rtos` 不作修改。

```
project/amebadplus/
  bootloader_km0/       KM0 第一阶段启动对象、独立 CMake 入口
  bootloader_km0_2nd/   KM0 第二阶段启动对象、独立 CMake 入口
  bootloader_km4/       KM4 image1、双核加载和安全启动
  app_km0/             KM0 image2、Wi-Fi 固件和共存 IPC
  app_km4/             共享 src/main.cpp、平台服务和 TF-M 客户端
  app_km4_secure/      TF-M SPE、平台隔离和安全存储驱动
  quic_perf/           本地 QUIC 测试服务端
  ameba_layout.ld      双核 SRAM/XIP 布局
  config.cmake         安全/非安全编译配置
  features.cmake       可选功能接入
  Makefile             单镜像和整机构建入口
  flash.ps1            自动下载、读回校验和启动检查
  serial_cmd.ps1        串口命令及日志
  cli_test.ps1          控制台与配置持久化验证
  boottest.ps1          连续复位、TF-M 状态和加密自测检查
  provision_security.ps1  显式分阶段 eFuse 写入和读回检查
  keys/com42.json       COM42 私钥，保存在本地仓库并被 .gitignore 排除
  keys/com42.public.json  可公开的 root/app 公钥和哈希
  hci_test.ps1          原始 H4 命令和 DTM 检查
```

六个镜像目录与 AmebaD 对齐。Dplus ROM 的实际格式不同：两个 KM0 启动阶段作为对象编入 KM0 image2，独立烧录的引导镜像是 KM4 image1。TF-M 随 APP_ALL 尾部的 IMG3 一起升级，不使用 MCUboot BL2。

## 构建

需要 CMake 3.24+、Ninja、Python 3 和 ASDK 10.3.1-4523。工具链查找 `toolchain/asdk-10.3.1-4523/linux/newlib` 或 `/opt/rtk-toolchain/asdk-10.3.1-4523/linux/newlib`，可用 `AMEBAD_TOOLCHAIN_ROOT` 覆盖。

```sh
make -C project/amebadplus deps
make -C project/amebadplus tfm_deps       # 使用 TFM/STORAGE 时安装
make -C project amebadplus
make -C project/amebadplus app_km4        # 单独构建指定镜像
make -C project/amebadplus app_km4_secure STORAGE=ON
make -C project/amebadplus test
make -C project/amebadplus STORAGE=ON AWS=ON MCUMGR=ON HWGCM=ON
```

默认输出 `build/amebadplus`，用 `BUILD=/absolute/path` 隔离变体。选项会在重新配置时更新；TF-M 使用构建目录内的私有 mbedTLS 补丁副本，并检查源文件指纹，避免污染共享子模块或复用过期副本。

| 选项 | 对应功能 |
|---|---|
| 默认 | FreeRTOS 11.1、TLSF、FreeRTOS+TCP、LittleFS/POSIX、CLI、Wi-Fi 重连、802.11d、SNTP/RTC、iperf、ZIP、Flash 写保护和 SleepCG |
| `NIMBLE=ON` | NimBLE、BLE 控制器、Wi-Fi/BT 共存、绑定存储、GATT、bletp |
| `MCUMGR=ON` | BLE SMP 文件管理/OTA；自动启用 NimBLE |
| `HCI=ON` | LOGUART 原始 H4 控制器桥接和 DTM；与 NimBLE 互斥 |
| `AWS=ON` | AWS IoT telemetry、配置 CLI、HTTP OTA |
| `AWSOTA=ON` | coreMQTT-Agent 和 FreeRTOS OTA Agent；自动启用 AWS |
| `TLS_BACKEND=picotls/mbedtls/fallback` | TLS 传输后端，默认 picotls；`CRT_BUNDLE=ON` 使用证书包 |
| `TLS_HEAP_TRACE=ON` | picotls 握手堆使用日志 |
| `TFM=ON` | TF-M 2.1 SPE、TrustZone 隔离、PSA 调用与受限 Flash 服务 |
| `STORAGE=ON` | 自动启用 TF-M；ITS 最大 2048 字节、加密 PS 最大 4096 字节；AWS 私钥改用 ITS |
| `SBOOT=ON` | 默认启用；Ed25519 签名 boot、应用 manifest 和应用证书链 |
| `RSIP=ON` | COM42 已烧熔丝，默认开启；AES-XTS 加密 boot/KM0/KM4/TF-M，打包时独立解密比对 |
| `HWGCM=ON` | ROM AES-GCM DMA 加速；不适用的长度/IV 和 multipart 调用走软件实现 |
| `ROMKAT=ON` | ROM AES-GCM/SHA-256 和上层 GCM 已知答案测试 |
| `WPS=ON` | `wps pbc`、`wps pin [PIN]` |
| `EAP=ON` | WPA2-Enterprise EAP-TLS，验证 CA 和服务端名称，支持循环连接与停止 |
| `QUIC=ON` | ngtcp2/picotls QUIC 上下行测试；`QUIC_SERVER=a.b.c.d` 指定主机 |
| `PKCS11=ON` / `BENCH=ON` | PKCS#11 自测 / mbedTLS 基准测试 |
| `CRYPTO_SELFTESTS=ON` | bench、PKCS#11 和证书包诊断 CLI |
| `VFSTEST=ON` / `CPPTEST=ON` | POSIX 文件/套接字及 C++ 文件系统/iostream 自测 |
| `CARES=OFF` | 切换为 FreeRTOS+TCP DNS |
| `IPERF=OFF` / `MINIMAL=ON` | 关闭 iperf 等默认诊断入口 |
| `EFUSE_DEBUG=ON` | 调试用 OTP 读写 CLI；写入必须显式带 `CONFIRM`，默认关闭 |

可选功能不要求同时开启。APP_ALL 的硬上限为 `0x170000` 字节，打包器会拒绝超出槽位的组合。`AWS` 默认关闭 iperf，并使用与 AmebaD 相同的小 TCP 缓冲配置，为 TLS、BLE 和 OTA 留出堆空间。`FW_VERSION` 是应用显示版本，`APP_BUILD` 是 AWS OTA 应用构建号；它们不同于 ROM manifest 的镜像版本。

## 烧录和运行

```powershell
.\project\amebadplus\flash.ps1 -Port COM42 -FirmwareOnly -AllowEncrypted
.\project\amebadplus\cli_test.ps1 -Port COM42
.\project\amebadplus\boottest.ps1 -Port COM42 -Count 32 -Storage -Crypto -Entropy
.\project\amebadplus\serial_cmd.ps1 -Port COM42 -Cmds @('version','heap','date','flash status')
```

RTS 控制复位、DTR 控制下载，默认下载和控制台速率为 1500000。`-BootPort` 可使用另一控制端口。下载成功后还要通过读回校验和应用启动检查。

- `Image_All.bin`：4 MiB，含资源 LittleFS；整片下载重置文件、配置和 PSA 存储。
- `firmware.bin`：boot + OTA1；`-FirmwareOnly` 保留 OTA2、LittleFS 和 PSA 存储。
- `ota_all.bin`：32 字节传输头 + APP_ALL，仅用于运行中的 HTTP/AWS/BLE OTA，不包含 boot。

`Image_All.bin` 包含 `project/resource` 的原始内容。若该目录保留明文客户端私钥，整片烧录也会将其写入 LittleFS；启用 ITS 后应移除设备上的明文副本。`firmware.bin` 不包含这些资源，适用于保留已配置安全存储的更新。

热点通过设备上的 `setenv WIFI_SSID ...`、`setenv WIFI_PASSWORD ...` 配置，不写入源码。`WIFI_COUNTRY` 可指定国家代码；未指定时根据扫描的 Country IE 选择。`dsleep uart`、`dsleep rtc 3` 使用 SleepCG 保持运行状态，输出实际进入次数；SleepCG 期间 UART 由定时唤醒后读取 FIFO，输入响应延迟约 100 ms，普通控制台使用中断。该模式不等于整机掉电。启动时自动锁定底部固件区；`flash lock` 可重新锁定，`flash unlock` 解锁；OTA 会临时解除并恢复固件写保护。`flash.ps1` 会先通过 CLI 解锁，再进入 ROM 下载，避免原厂工具只清 BP、未清 CMP 导致校验失败；`-NoUnlock` 可跳过。原始 HCI 诊断模式没有 CLI，启动时保持 Flash 可写，以便随后重新烧录。

EAP 使用设备 `/eap/ca.pem`、`/eap/client.pem`、`/eap/key.pem`，并配置 `EAP_SERVER_NAME`，运行 `eap <ssid> <identity>`、`eap loop <ssid> <identity>` 或 `eap stop`。测试证书和私钥不纳入源码。

BLE GATT 可用 `nimble_ble_verify.py` 检查；SMP 升级使用 `mcumgr_upload.py --name <广播名> --image <ota_all.bin>`。HCI 模式使用 `hci_test.ps1`。QUIC 主机程序使用仓库现有 host ngtcp2/wolfSSL 库构建，测试时指定局域网接口和允许连接的板卡 IP，并用 `timeout` 限定服务时间；它是吞吐测试，客户端不验证测试服务端证书。

`AWSOTA=ON` 的 Job 通知和状态上报使用 MQTT，固件通过 S3 预签名 HTTPS URL 下载。AWS API 中选择 `protocols: ["HTTP"]`；Dplus 会拒绝明文 HTTP Job URL。原生 OTA Agent 每次请求 4 KiB，PAL 从 Flash 回读完整文件并验证内置签名证书对应的 ECDSA/SHA-256 签名，之后才提交新槽。新镜像重启且启动自测通过后才确认成功。下载时暂停 BLE 释放内存，重启后恢复。

云端请求格式见 [ota_job.example.json](ota_job.example.json)：替换账户、Thing、桶、对象版本、签名 profile 和角色后，用 `aws iot create-ota-update --cli-input-json file://job.json` 创建只针对该 Thing 的 SNAPSHOT Job。上传的是 `ota_all.bin`；`fileVersion` 与递增的 `APP_BUILD` 对应。服务端证书验证和固件签名校验均启用，设备日志不输出临时下载 URL。AWS 参数说明见 [官方 OTA CLI 文档](https://docs.aws.amazon.com/freertos/latest/userguide/ota-cli-workflow.html)。

## Flash 和安全边界

| 物理偏移 | 区域 |
|---|---|
| `0x000000..0x00dfff` | KM4 boot |
| `0x00e000..0x00ffff` | SDK 系统/校准保留 |
| `0x010000..0x17ffff` | OTA1 APP_ALL |
| `0x180000..0x18ffff` | SDK OTA2 boot 保留 |
| `0x190000..0x2fffff` | OTA2 APP_ALL |
| `0x300000..0x3effff` | LittleFS 960 KiB |
| `0x3f0000..0x3f3fff` | TF-M ITS 16 KiB |
| `0x3f4000..0x3fdfff` | TF-M PS 40 KiB |
| `0x3fe000..0x3fffff` | 开发用 HUK/计数器和备份 |

TF-M 使用隔离级别 1：安全分区共享安全地址空间，SAU 和锁定的 MPC/PPC 隔离 NS；Flash 修改经检查后的 PSA 服务执行。NS 不可修改 boot、当前运行槽位、PSA 区域或安全映射。OTA 先擦除目标槽证书标记，完整接收、格式/校验和/Flash 回读检查通过后提交目标证书，再使旧槽失效；不支持跨 TF-M/非 TF-M 启动 ABI 的 OTA。

COM42 已永久启用 SBOOT 和 RSIP，后续固件必须使用 `SBOOT=ON RSIP=ON` 及匹配密钥。按用户要求，原有密钥保存在 `project/amebadplus/keys/com42.json`（权限 0600，仅保存在本地仓库，被 `.gitignore` 排除），包含 root/app Ed25519 种子、两组 RSIP 256 位密钥和 boot/app IV；`SECURITY_KEYS` 可指定其他设备的密钥文件。公开仓库仅包含 `keys/com42.public.json` 公钥与哈希。缺少私钥时构建会拒绝签名/加密，不会自动生成替代密钥；全新未熔丝板可用 `SBOOT=OFF RSIP=OFF` 构建明文镜像。普通构建不烧写 eFuse，不要替换已烧录设备的密钥文件。

本机保留含原密钥的本地 Git 引用 `refs/local/com42-security-snapshot`，该引用不随普通分支 push 发布。私钥仍位于上述原路径；换机器构建 COM42 固件需要单独安全导入同一密钥文件。

`make flashloader` 将 SDK ImageTool 复制到构建目录，用同一 root 密钥签名 RAM 下载器并独立验签。`make flash` 构建签名下载器，以 firmware-only 方式保留持久数据；`RSIP=ON` 时传入 `-AllowEncrypted`。直接执行 PowerShell 时先准备签名下载器；默认读取 `build/amebadplus/security-tools`，其他构建目录用 `-ToolDir` 指定。熔丝启用 SBOOT 后，原厂未匹配签名的下载器不能用于恢复。

`provision_security.ps1` 仅在显式指定 `-Stage Keys/Sboot/Rsip/Protect -ConfirmBurn -PublicHash <SHA256>` 时写 OTP。每阶段先检查密钥槽，再逐项读回；保护阶段永久锁定 PK1 写入、RSIP 密钥读写及模式位，之后不能重复读取密钥来执行该阶段。COM42 的物理地址为 PK1 `0x320..0x33f`、RSIP `0x2c0..0x2ff`、使能 `0x368=F3`、保护 `0x365=FD / 0x366=7F / 0x367=F0`；其余安全配置保持原值。位定义参照 [Realtek SBOOT](https://ameba-aiot.github.io/ameba-iot-docs/freertos/en/latest/rst_rtos/3_nda_secure_boot/0_secure_boot_index_nda.html) 和 [Realtek RSIP](https://ameba-aiot.github.io/ameba-iot-docs/freertos/en/latest/rst_rtos/3_nda_rsip/0_rsip_index_nda.html)。

加密 OTA 的校验和与 AWS ECDSA 验签针对 Flash 上的密文。TF-M 在提交前通过 OTP 硬件密钥解密并检查加载头，只返回校验结果；不向 NS 返回密钥或解密内容，也不更改运行镜像的 MMU/RSIP 映射。重启后 SBOOT 再验证解密后的完整应用。

开发板上的 HUK 由硬件 TRNG 生成并保存在安全 Flash，未烧入 OTP。PS 使用认证加密；ITS 不自带静态加密。当前 RSIP 只覆盖启动/应用/TF-M 代码区，未覆盖 ITS、LittleFS 和开发 HUK 区，不能宣称它们抵御物理 Flash 读取。安全区初始化清除旧 TRNG FIFO 并等待异步复位，运行期间的健康检测错误和超时会失败，不回退到伪随机数。

实测结果与未覆盖范围见 [VALIDATION.md](VALIDATION.md)。旧 `project/ameba` 已删除；其 Green2 配置保留在 `project/amebagreen2`，不属于 COM42 验证范围。
