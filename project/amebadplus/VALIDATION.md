# COM42 移植与验证记录（2026-11-15～16）

本记录区分实机、主机验证和未覆盖项目。日志均位于忽略目录 `build/amebadplus/`。COM42 的 SBOOT/RSIP 密钥保存在本地 `keys/com42.json`，已备份至私有仓库 [at-project-com42-keys](https://github.com/zxf1023818103/at-project-com42-keys)；公钥/哈希记录为 `keys/com42.public.json`，本机另保留 Git 备份引用。硬件为 AmebaDplus / RTL8711Dx，4 MiB NOR（ID `20-40-16`），KM4 240 MHz / KM0 96 MHz；RTS/DTR 自动下载，串口 1500000。

## 结构和功能范围

`bootloader_km0`、`bootloader_km0_2nd`、`bootloader_km4`、`app_km0`、`app_km4`、`app_km4_secure` 六个工程目录与 AmebaD 对齐。Dplus 的两个 KM0 启动阶段作为对象编入 KM0 image2，TF-M 作为 IMG3 随 APP_ALL 升级，这是 ROM 格式差异。旧 `project/ameba` 已删除，Green2 配置另存 `project/amebagreen2`。

AmebaD Makefile 的可选功能已逐项接入：NimBLE、HCI、QUIC、PKCS11、BENCH、MINIMAL、ROMKAT、IPERF、CARES、WPS、EAP、TFM、STORAGE、AWS、AWSOTA、TLS_BACKEND、CRT_BUNDLE、TLS_HEAP_TRACE、HWGCM、VFSTEST、CPPTEST、SBOOT、RSIP、EFUSE_DEBUG、CRYPTO_SELFTESTS、MCUMGR。具体开关和芯片差异见 README。可选功能允许分别构建，不代表任意组合都能装入一个 OTA 槽。

## 最终完整固件（SBOOT + RSIP）

```sh
make -C project/amebadplus BUILD="$PWD/build/amebadplus/complete" \
  STORAGE=ON AWSOTA=ON MCUMGR=ON HWGCM=ON ROMKAT=ON \
  WPS=ON EAP=ON TLS_HEAP_TRACE=ON SBOOT=ON RSIP=ON EFUSE_DEBUG=OFF \
  FW_VERSION=dplus-sec-B APP_BUILD=10 IPERF=OFF
```

- `complete/firmware.bin`：1,563,168 B，SHA-256 `6b422926d2fb33d26dd72358fdc63be35171bcbc721b174ec2e4a13f42e421ca`。
- `complete/ota_all.bin`：1,497,664 B，SHA-256 `e8be36c6283e3a1848e08663cac571f91501cfaa0ab6304d9df80ee9d935c727`。
- APP_ALL：1,497,632 B，槽内余量 9,696 B；boot：30,944 B。已保留一致副本于 `security-b-encrypted/`。
- 最终 NS/SPE ELF 中均无调试 OTP 读写实现；主机镜像/签名 7 项、两种 NOR OTA ABI 和 OTP 参数测试通过（`security-host-tests.log`），非 TF-M 构建通过（`security-nontfm-build.log`）。

## HTTPS Job B7 固件（eFuse 配置前）

```sh
make -C project/amebadplus BUILD="$PWD/build/amebadplus/complete" \
  STORAGE=ON AWSOTA=ON MCUMGR=ON HWGCM=ON ROMKAT=ON \
  WPS=ON EAP=ON TLS_HEAP_TRACE=ON RSIP=OFF FW_VERSION=dplus-job-B APP_BUILD=7 IPERF=OFF
```

- `build/amebadplus/job-b-r4/firmware.bin`：1,562,400 B；SHA-256 `31c0e72b61d3a8b98278363c0e1fc2a6a7f890d2086a48b1bba34ba8385be10c`。
- `build/amebadplus/job-b-r4/ota_all.bin`：1,496,896 B；SHA-256 `55b2fe7beb706236cfec68f4497c082d4e3fe7625e6fae662d53d60aa625cc23`。
- APP_ALL 为 1,496,864 B，距 `0x170000` 槽上限余 10,464 B；boot 为 30,944 B。
- 迁移基线的默认配置已重新构建，APP_ALL 为 795,616 B；自动解锁、烧录校验、锁定启动及 Wi-Fi 联网通过（`default-final-build.log`、`default-latest-flash.log`）。
- 此组合关闭 HCI（与 NimBLE 互斥）、QUIC 吞吐循环和额外诊断自测；这些选项由单独变体验证。

## 已完成的实机检查

下表保留迁移基线 `dplus-full / build 5` 的覆盖记录；eFuse 配置前的 HTTPS Job 固件为 `dplus-job-B / build 7`。后续安全启动与加密验证单独记录。

| 项目 | 实测结果 | 日志 |
|---|---|---|
| 自动烧录、读回校验、双核启动 | SUCCESS；FreeRTOS 11.1 / 共享 TLSF / FreeRTOS+TCP | `final-restored-flash.log`、`final-restored-boot.log` |
| CLI、LittleFS、环境变量 | 文件操作及复位持久化通过 | `cli-test.log`、`network-final.log` |
| Wi-Fi WPA2 / DHCP / DNS / SNTP | Zeng 连接成功；设备 IP `192.168.100.7`；解析和校时通过 | `network-final.log`、`final-restored-boot.log` |
| NimBLE 软件加密 | AES、CMAC、ECDH、P-256 全通过 | `boot-final.log` |
| BLE 扫描、连接、GATT 写入回读 | `a5` 写入及回读通过；广播名 `amebadplus_f4718a` | `ble-test.log` |
| Wi-Fi 与 BLE 共存 | BLE 广播时 TCP 发送 15 秒，约 11,816 Kbit/s | `tcp-device.log`、`tcp-host.log` |
| QUIC / HWGCM | 双向各 4,194,304 B；下载 5561 Kbit/s，上传 922 Kbit/s，主机计数一致 | `quic-verified.log`、`quic-host-verified.log` |
| HCI / DTM | 严格 H4 帧检查；Reset、版本、64 B Supported Commands、地址、LE 能力及 DTM RX/End 全通过 | `hci-final-test.log` |
| WPS PBC | 真实 RouterOS AP 握手，result=0，保存凭据 | `wps-test.log` |
| EAP-TLS | 新生成测试 PKI；真实 RADIUS 握手通过，4 次循环通过 | `eap-test.log`、`eap-loop.log` |
| EAP 服务端名称校验 | 错误名称连接被拒绝；停止后恢复普通 Wi-Fi | `eap-reject.log`、`eap-stop.log` |
| TF-M 调用和隔离边界 | PSA ping；安全 RAM、溢出指针、boot/当前槽/安全存储写入请求均被拒绝 | `final-restored-boot.log` |
| ITS / PS | 1537 B 记录跨复位保留；ITS 2048 B、PS 4096 B 的覆盖、部分读取、越界、删除及 write-once 通过 | `final-restored-boot.log` |
| TRNG / 安全存储连续复位 | 迁移基线 build 5 的 32/32 次通过；每次 ITS/PS、SHA/GCM 与 64 KiB TRNG 检查通过，累计 2 MiB 熵健康检查 | `final-soak.log`、`final-soak/reset-001.log` 至 `final-soak/reset-032.log` |
| ROM / 硬件 GCM | SHA-256、AES-128/192/256-GCM、AAD、非整块、非对齐、短 tag、原位操作、坏 tag 清零及 multipart 通过 | `final-restored-boot.log` |
| AWS ITS 私钥后端 | 经明确授权写入仓库现有凭据；TLS 服务端证书验证、MQTT 连接和 telemetry 通过 | `storage-aws-test.log` |
| AWS HTTPS OTA Job | A6→B7，366 分块 / 1,496,896 B；S3 TLS、ECDSA 验签、新槽自测及云端 SUCCEEDED 全通过 | `job-https-b7-r4.log`、`job-b-r4-execution.json` |
| HTTP OTA / TF-M | OTA1→OTA2，约 1.22 MB，4.65 秒；ITS/PS 保留，AWS 使用原 ITS 密钥重连 | `tfm-ota-http-test.log` |
| BLE SMP OTA / TF-M | OTA2→OTA1，1,220,960 B，约 325 秒；新版本 telemetry 确认启动及原密钥保留 | `tfm-smp-upload.log`、`tfm-smp-after-reset.log` |
| PKCS#11 / BENCH / VFS / C++ | 密钥对象自测、加密基准、POSIX 文件/套接字、filesystem/iostream 通过 | `diagnostics-boot.log` |
| Flash 保护 | 固件区 lock/unlock 状态读回正确；锁定时仍能修改 LittleFS 配置；TF-M 网关也通过 | `power-test.log`、`complete-power.log` |
| 定时 SleepCG | 非 TF-M / TF-M 均实际进入并在约 3 秒恢复；TF-M 下 AWS 任务继续运行 | `power-test.log`、`complete-power.log` |
| UART SleepCG | 8 秒超时前输入字符恢复，后续控制台及 AWS 遥测正常；FIFO 定时读取约 100 ms 响应 | `complete-power-irqclear-run.log` |
| eFuse 调试 | 实机只读及越界读取拒绝通过；没有执行 OTP 写入 | `efuse-readonly-test.log` |

路由器临时 VAP、RADIUS 配置已移除，原配置已恢复（`router-restore-verified.log`）。热点密码只保存在设备配置中。

`boottest.ps1` 在启动完成后读取 `tfm_status`，确认 `selftest=3 entropy=65536`；首次查询若早于安全存储测试完成，会在同一次启动结束后再次查询。双核启动日志可能按字节交错，状态查询用于核对完成状态。基线 build 5 的 32 次复位均通过，未发现断言或 TRNG 错误。

基线完整组合另完成约 1.50 MB 的 OTA1→OTA2 HTTP 升级：锁定固件区后开始更新，升级后仍为 `SR1=0x14 / SR2=0x42`，ITS/PS 数据保留，AWS 自动重连并发送 telemetry（`final-http-ota.log`、`final-after-ota.log`）。随后 OTA2→OTA1 反向升级也通过，`tfm_status` 返回 `selftest=3 entropy=65536`（`final-return-ota.log`、`final-return-after.log`）。设备 LittleFS 上的 `/aws_key.der` 明文副本已移除，仓库原件保留，重启后仍由 ITS 提供密钥。MQTT 运行时可用堆约 77,260 B。

基线交付检查（`final-delivery.log`）：COM42 运行 `dplus-full`，连续复位后仍从 ITS 读取原密钥并发送 AWS telemetry（seq 7～13）；Flash 保持 `0x14 / 0x42` 锁定，`tfm_status` 为 `selftest=3 entropy=65536`，可用堆 77,136 B，SNTP 已校时，`/aws_key.der` 不存在。HCI→全功能固件的自动下载、读回校验及启动也已通过。

## HTTPS OTA Job 实测

- Job：`AFR_OTA-dplus-https-20261115-b7-r4`；唯一目标 Thing `94c960f4718a`（COM42），SNAPSHOT。
- 设备从 `dplus-job-A / APP_BUILD=6`（OTA1）升级到 `dplus-job-B / APP_BUILD=7`（OTA2），数据通过 S3 HTTPS 下载；Job 通知和状态上报通过 MQTT/TLS。
- AWS Signer 使用现有 `atProjectOtaV2`，公开证书与固件内置证书一致。366 个分块的序号和总字节数核对通过；S3 服务端证书验证通过，Flash 回读 SHA-256 / ECDSA 验签通过后才提交。
- 首块 23:22:01.554，末块 23:27:55.812；23:27:56.402 签名通过，23:27:56.510 提交 OTA2；重启自测通过后，AWS 在 23:28:24.663 接收 `SUCCEEDED`，details 为 `accepted v.9.7`。以上均为 2026-11-15 CST。
- 再次 RTS 复位后仍运行 B7，TF-M/ITS/PS、BLE 广播、AWS telemetry 恢复；固件区锁定，私钥仍从 ITS 使用，设备 `/aws_key.der` 不存在。`OTA_SELFTEST=0`、目标槽标志已清除、时间已同步、运行堆余量 67,924 B（`job-final-reset.log`、`job-final-steady.log`）。
- 云端证据：`job-b-r4-execution.json`、`job-executions-final.json`；汇总及固件 SHA-256：`job-result.json`。本次只创建单设备 Job，未修改现有 IoT 策略或 IAM 角色。

实测发现并修复了以下问题：Dplus HTTPS Job 原先绕过签名 PAL、仅凭保存的 Job ID 确认成功，现在使用原生 OTA Agent 的下载、验签和启动自测流程；待确认状态记录目标槽，旧槽中断重启不能伪装成新镜像。4 KiB 分块下载采用较小的 TLS 接收预留，避免第二条 TLS 连接内存不足；接收包装器在有界超时内等待 TLS 分片形成明文，避免 coreHTTP 默认 10 ms 零字节重试窗口造成响应截断。S3 关闭 keep-alive 后，同一 Range 自动重连重试通过。

早期 `b7`、`b7-r2`、`b7-r3` 排错 Job 均已取消；最终 `b7-r4` 为设备自行上报的成功，没有通过主机调用 UpdateJobExecution 修改成功状态。临时预签名 URL 已从旧日志移除，新日志不再打印 Job 文档字段。请求示例见 `ota_job.example.json`，参数语义参照 [AWS 官方 OTA CLI 文档](https://docs.aws.amazon.com/freertos/latest/userguide/ota-cli-workflow.html)。

## 主机和构建检查

- `make -C project/amebadplus test`：镜像/签名 7 项、NOR OTA 两种 ABI、OTP 参数检查通过。OTA 与 OTP 的 C 测试使用 ASan/UBSan，覆盖不完整镜像、错误入口、SPE 缺失/截断、校验和、边界、擦写/提交失败；OTP 测试使用 stub，不接触硬件。
- Ed25519 boot / 证书链 / 应用签名和 SHA-256 校验通过；修改证书、密钥或载荷会被主机验证器拒绝。
- RSIP XTS 打包并独立解密比对通过，包括带 TF-M 的组合（`rsip-tfm-pack.log`、`security-a-encrypted-pack.log`）。COM42 实机加密启动验证见后文。
- `STORAGE` 在同一构建目录 ON→OFF→ON 重新配置和编译通过（`tfm-toggle-off-build.log`、`tfm-toggle-on-build.log`）。
- `TLS_BACKEND=mbedtls`、`TLS_BACKEND=fallback` 及 `CARES=OFF` 已编译通过（`tls-mbedtls-build.log`、`tls-fallback-build.log`）；云端运行记录使用 picotls。
- 本次 Job 修正后主机测试再次通过（`job-host-tests.log`），原 AmebaD `target_img2_km4` 编译回归通过（`job-amebad-regression.log`）；`platform/ameba-rtos` SDK 保持未修改。

## 回归中修复的问题

TF-M PS 默认 32 B 缓冲导致每次分块重写整份文件，改为 4352 B 缓冲；安全 Flash 擦写补齐 RSIP 缓存失效。SHA ROM 测试使用正确的 SHA 引擎初始化。SDK 引导代码把 RCT1 阈值设为 3，曾造成间歇性启动失败；安全熵驱动恢复 `ameba_trng.h` 记录的默认值 5，保留所有健康检测，首次复位等待稳定，运行失败不清错重试或回退伪随机数。

早期仅 10 次复位通过后仍复现过 TRNG 故障（`tfm-smp-persistence.log`）；上面的连续复位记录来自修复后的版本，不能将早期记录视为长期稳定性证明。

QUIC 主机工具补齐断开连接的超时退出，并在接收 FIN 后保留短暂 ACK 发送窗口，避免完整接收后遗漏最后一个确认包。

UART 休眠回归发现接收超时标志未清除会造成中断持续挂起。中断处理现显式清除 timeout/line-status 标志、限制单次接收数量；SleepCG 时保留 FIFO 数据，定时唤醒读取，恢复控制台前清除旧标志。原厂 Shell 的 PB4 GPIO 路径和 KM0 控制台中断关闭。

固件区自动锁定后，原厂下载工具只处理 BP，未清除 CMP，曾导致写入校验失败。`flash.ps1` 现在像 AmebaD 一样先发送 `flash unlock`，然后才进入 ROM 下载。HCI 原始串口模式启动时保持解锁，以支持后续重新烧录。

AWS 完整组合启用了与 AmebaD 相同的 `AT_NET_LOW_MEM` TCP 配置，避免默认双向各 35 KiB 缓冲占满堆、导致 HTTP OTA 任务无法创建。

## COM42 eFuse / SBOOT / RSIP 实测

2026-11-15 根据用户明确授权烧写；保护位另经确认。烧写前 PK1、两组 RSIP 密钥和 `0x360..0x36f` 均为 `FF`，逻辑 `0x000..0x007=ffff01e0ffffffff`。

| 项目 | 已验证结果 | 证据 |
|---|---|---|
| 密钥持久保存 | 原有 root/app Ed25519 和 RSIP 密钥保存在 `project/amebadplus/keys/com42.json`，本机权限 0600，另有私有仓库备份；与镜像及下载器一致 | `security-a-encrypted-pack.log`、`flashloader-prepare.log`，私有备份提交 `d2dc7f1` |
| PK1 | 物理 `0x320..0x33f` 的 SHA-256 为 `50591c9571de24ec3c675a0fbf9b218b812c63edbb30140cf7210d2bfc6d3062`；写后读回一致 | `fuse-keys.log` |
| RSIP 密钥 | `0x2c0..0x2df`、`0x2e0..0x2ff` 写后读回一致；数值不写入报告 | `fuse-keys.log` |
| 永久使能 | `0x368=F3`，SBOOT 和 RSIP 均启用，XTS 模式 | `fuse-sboot-enable.log`、`fuse-rsip-enable.log` |
| 密钥保护 | `0x365=FD / 0x366=7F / 0x367=F0`；复位后两个密钥槽均读为全 FF，不再返回实际值 | `fuse-key-protection.log`、`rsip-locked-reset.log` |
| SBOOT 正向 | ROM `IMG1 SBOOT EN`、`IMG1 VALID`、应用 `IMG2 VERIFY PASS` | `sboot-first-boot.log` |
| 应用签名篡改 | 仅翻转一个签名字节，`IMG2 VERIFY FAIL, ret=-3`；另一有效槽仍通过验签并启动 | `sboot-tamper-app-boot.log` |
| boot 签名篡改 | ROM `IMG1 INVALID, ret=-3`，`BOOT FAIL`，未进入应用 | `sboot-tamper-boot-boot.log` |
| 签名下载器恢复 | 已熔丝设备使用同一 root 签名的 RAM flashloader，串口烧写/读回校验/启动通过 | `sboot-recovery-flash.log`、`sboot-recovery-boot.log` |
| RSIP 实机解密 | boot `OTF EN`，KM0/KM4/TF-M 三段 `IMG2 OTF EN`，随后 `IMG2 VERIFY PASS` | `rsip-first-boot.log` |
| 保护后复位 | SBOOT、RSIP、TF-M/ITS/PS 自测、Wi-Fi/BLE、AWS telemetry 通过；`TFM selftest=3 entropy=65536` | `rsip-locked-reset.log` |

下载器签名工具只修改构建目录中的 SDK 副本。普通构建默认 `SBOOT=ON RSIP=ON EFUSE_DEBUG=OFF`，不会写 OTP；烧写脚本只在显式指定阶段与 `-ConfirmBurn` 时执行。未启用 anti-rollback、SWD 禁用或 OTP HUK。应用签名拒绝时能够启动另一有效槽，不代表 OTA 提交后具备自动回滚。

## 开启密钥保护后的加密 HTTPS OTA Job

- 唯一目标为 COM42 / Thing `94c960f4718a`；Job `AFR_OTA-dplus-https-rsip-20261115-b10`，云端最终 **SUCCEEDED**，设备上报 `accepted v.9.10`，旧版本 `v.9.9`。
- `dplus-sec-A / APP_BUILD=9` 从 OTA1 接收 366 个分块，共 1,497,664 B；末块 2624 B。对下载密文逐字节 Flash 回读后进行 AWS ECDSA/SHA-256 验签；在密钥已读保护的情况下，通过 OTP 硬件 AES 校验 KM0/KM4/TF-M 加载记录，随后校验和通过并提交 OTA2。
- 2026-11-16 00:03:58.949 签名通过，00:03:59.056 提交 `0x190000`；重启时 boot 和三个应用区域启用 OTF，`IMG2 VERIFY PASS`，ITS/PS 的 1537 B 持久记录及 64 KiB TRNG 自测通过。00:04:25.174 新固件自测接受镜像，AWS 在 00:04:26.238 记录成功。时间均为 CST。
- TLS 服务端证书验证保持启用；Job 控制和状态走 MQTT/TLS，固件数据走 S3 HTTPS。下载中的连接中断通过同一 Range 重连重试恢复，所有分块和总长度均核对一致；没有主机伪造成功状态。
- 使用用户确认的现有 S3 桶 `amebad-ota-991983452270`、现有角色及签名配置；只上传加密固件，未上传 eFuse 密钥。无新的 IAM/IoT 策略修改。
- 再次 RTS 复位后仍为 B10：SBOOT/RSIP 继续生效，`TFM selftest=3 entropy=65536`，ITS/PS 持久记录、BLE 广播和 AWS telemetry 正常；Flash 为 `14/42` 锁定，`OTA_SELFTEST=0` 且目标槽标志清空，设备 `/aws_key.der` 不存在，调试 eFuse 命令已移除。查询可用堆 68,016 B；云端无遗留执行中的 Job（`security-final-reset.log`）。
- 证据：`rsip-job-b10.log`、`security-b-execution.json`、`security-jobs-final.json`、`security-result.json`。

## 未覆盖范围和限制

- AWS Job 已实测 HTTPS 正向升级与重启确认；COM42 的 MQTT 固件数据面、升级中断电及坏签名/过期 URL 故障注入未实测。尚无启动失败后的自动回滚流程。WPS 实测为 PBC，PIN 分支未做真实配对。
- SleepCG 保留运行状态，不等于整机掉电；未测电流，也未验证长时间低功耗驻留或长期可靠性。
- 开发 HUK 使用 TRNG 生成并保存在安全 Flash，未烧入 OTP；PS 有认证加密，ITS 不自带静态加密。本次 RSIP 保护代码区，不覆盖 ITS、LittleFS 或开发 HUK 区，因此不能宣称这些区域抵御物理 Flash 读取。
- TF-M 隔离等级为 1，SPE 分区共享安全地址空间；安全 SRAM 使用较满，扩展安全服务时必须重新检查链接余量与栈使用。

## 发布前检查

公开代码仓库通过 `.gitignore` 排除 COM42 私有签名密钥和 RSIP 密钥。私有备份仓库的可见性已确认为 `private`；重新克隆提交 `d2dc7f1a80c4b201b610eb695870e02325c4b76b`，私钥、公钥清单、校验和及恢复说明均与本地备份逐字节一致，私钥文件 SHA-256 与实测记录 `security-result.json` 相符。本机另保留 `refs/local/com42-security-snapshot` 和 `refs/local/com42-owner-key-backup` 引用。缺失密钥时构建拒绝自动生成替代密钥；主机镜像/签名 8 项及原 NOR/OTP 测试通过（`security-publish-tests.log`）。
