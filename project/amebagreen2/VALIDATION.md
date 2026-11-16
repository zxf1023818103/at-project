# Green2 实机验证

测试日期：2026-11-16。板卡为 RTL8711F B-cut，4 MiB Puya NOR，Wi-Fi MAC `94:c9:60:59:06:fe`。COM15 下载/控制台 1500000 baud，COM19 RTS 复位、DTR 下载控制。测试日志和固件位于忽略的 `build/amebagreen2*`，不随源码提交。

## 构建隔离

将整个 `platform/ameba-rtos` 临时移到仓库外，分别从空目录构建默认配置和完整功能配置，全部核心、TF-M 和镜像打包通过。检查 14 个 `build.ninja`、`compile_commands.json`、`CMakeCache.txt`，没有原 SDK 或临时停放目录的引用；测试后 SDK 已恢复，子模块工作区干净。

完整配置：`STORAGE=ON AWSOTA=ON MCUMGR=ON WPS=ON EAP=ON HWGCM=ON ROMKAT=ON TLS_HEAP_TRACE=ON`。单独编译了 HCI、PKCS11、BENCH、QUIC、C++/VFS、MINIMAL、CARES=OFF，以及 picotls / mbedTLS / fallback TLS 后端。可选功能不是必须同时开启；打包器会拒绝超出 OTA 槽的组合。

休眠修复后再次移出 SDK，重编译默认及完整配置。最终配置额外启用 `CRYPTO_SELFTESTS=ON`，版本 `green2-final`、`APP_BUILD=7`；默认和完整配置均编译及打包通过。

证据：`isolation-build.log`、`isolation-final-build.log`、`no-sdk/default/`、`no-sdk/complete/`、`tls-mbedtls-build.log`、`tls-fallback-build.log`。

## 已通过的板卡测试

| 项目 | 覆盖 | 证据（相对 `build/amebagreen2/`） |
|---|---|---|
| 自动下载 | COM19 进入下载模式，COM15 烧录、逐字节读回、应用启动 | `flash-diagnostics.log`、`boot-diagnostics.log` |
| 双核及共享系统 | KM4NS/KM4TZ、FreeRTOS 11.1、TLSF、控制台、LittleFS | `boot-network-fixed2.log` |
| 网络 | Zeng 接入、DHCP、IPv6 link-local、DNS、SNTP | `boot-network-fixed2.log` |
| BLE | Windows 实际扫描、连接、GATT 写入 `0xa5` 并读回 | `ble-test.log` |
| HCI/DTM | H4 Reset、版本、支持命令、地址、LE buffer/features、DTM RX/end | `hci-test.log` |
| 加密 | ROM AES-128-GCM/SHA-256，GCM 非对齐、错误标签清零、分段、192/256 位、AAD、截短标签、原地操作；NimBLE AES/CMAC/P-256 | `boot-options3.log` |
| PKCS#11 | EC P-256 生成、签名、验签、LittleFS token 持久化 | `boot-diagnostics.log` |
| 基准与 C++/VFS | AES/ChaCha/SHA/ECDSA/ECDHE/RSA 基准；iostream、filesystem、stdio、POSIX fd/socket | `boot-diagnostics.log` |
| TF-M 网关 | PSA ping，安全/溢出缓冲区检查，拒绝启动区/活动槽/安全存储非法写入 | `boot-tfm-alias.log`、`boot-storage-clock.log` |
| TF-M 存储 | TRNG 生成开发 HUK，ITS/PS 首次写入及复位持久化，最大对象、覆盖、偏移读取、越界拒绝、删除和 write-once | `boot-storage-clock.log` |
| 重启稳定性 | 连续 10 次复位，每次 ITS/PS 自测、64 KiB 熵健康检查和硬件加密 KAT 全通过 | `storage-soak-final.log`、`storage-soak-final/` |
| HTTP OTA | 从 OTA1 下载至 OTA2，完整校验后提交，重启版本 `green2-ota-B` | `ota-http-device.log` |
| HTTPS OTA | 未受信 CA 被拒绝；添加本地测试根证书后 TLS 1.3 验证通过，`green2-sec-B` 镜像提交到 OTA2；ITS/PS 保留 | `ota-https-negative.log`、`ota-https-positive.log` |
| BLE OTA | SMP 传输 1,296,672 字节，约 310 秒；完整校验后由 OTA2 切回 OTA1，ITS/PS 保留 | `ota-ble-upload.log`、`ota-ble-device.log`、`post-ble-tfm.log` |
| 最终版本 OTA | HTTPS 下载包含休眠修复的 `green2-final` / build 7，提交到 OTA2，Wi-Fi 和全部启动自测通过 | `ota-https-final.log` |
| QUIC | ngtcp2/picotls TLS 1.3，双向各 4 MiB；下载 3357 kbps、上传 910 kbps | `quic-device-final.log`、`quic-host-final.log` |
| AP 休眠恢复 | RTC 3 秒：71 次 SleepCG，累计 3005 ms；UART 提前唤醒：31 次，累计 1401 ms；没有复位/异常 | `sleep-rtc.log`、`sleep-uart.log` |
| 唤醒后无线 | 再次 SleepCG 后 DNS 查询成功；BLE 扫描、连接、GATT 写 `0xa5` 并读回通过 | `post-sleep-dns.log`、`ble-post-sleep.log` |
| 最终 CLI/复位 | 版本、heap/tasks、文件环境变量写入、COM19 复位后读取、清理测试变量；TF-M 状态和 Flash 保护正常 | `final-device-test-pass.log`、`cli-final.log`、`final-status.log` |

休眠修复保留 TF-M 的安全权限，启用标准 FreeRTOS 定时唤醒，并让 NP 持续服务无线和看门狗。测试仅覆盖 AP 保留式 SleepCG，不覆盖整片断电休眠；功耗电流未测量。

最终文件位于 `build/amebagreen2/no-sdk/complete/`：

| 文件 | 字节数 | SHA-256 |
|---|---:|---|
| `firmware.bin` | 1,379,072 | `6649be326843a33a0330299d6a2bcbffc0b480a9335c875d25067342fbfcb4c5` |
| `ota_all.bin` | 1,313,568 | `0f5ff4646bea0b16707bf18c3c696f3fd5294ae9a6583468d5f396902152e8ba` |

## 主机安全及边界测试

12 个主机测试通过：镜像/签名/加密 9 个，OTA/NOR 2 个，eFuse 边界 1 个；NOR 和 eFuse 测试使用模拟后端及 ASan/UBSan，不访问真实 OTP。另以临时测试密钥验证 boot、NP、AP、SPE 的 Ed25519 签名和独立 AES-XTS 解密，并验证签名 RAM 下载器。

证据：`host-tests-final.log`、`security-image.log`、`security-tfm-final.log`、`security-flashloader.log`。COM15 没有烧写 eFuse，SBOOT/RSIP 的真实熔丝启动和锁定位不在此次实测范围内。

## 保留的数据与未验证项

烧录前整片备份：`com15-original-4m.bin`，SHA-256 `1d1c58c4f0c305681e9a696efa7272db21f30156de634e9c3b279310b4e93dcac`。烧录保留系统/射频校准区、LittleFS 和安全存储。未复用 COM42 的密钥，未将仓库 AWS 私钥配置到这块 Green2。

WPS/EAP 和 AWS Jobs 已编译及链接；WPS/EAP 实际网络握手、AWS 云端 Job 和设备证书配置尚未验证。低功耗电流未测量。zlib 保持任务开始前的状态。
