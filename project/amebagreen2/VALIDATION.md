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

## 2026-11-17：COM43 仅 BLE 广播功耗

此次使用另一块 Green2，用户提供的完整标记为 **RTL8711FAF-VA2-CG**，BLE MAC `94:c9:60:59:06:fd`。COM43 为下载/控制台；DTR 控制 LOG_TX 下载脚，BLU COM17 控制 3.3 V 断电/上电。本次未写 eFuse，保留已有射频校准和 LittleFS。

新增默认关闭的 `BLE_POWER_TEST=ON` 配置，与 `NIMBLE=ON MINIMAL=ON` 配合使用。WLAN 只初始化以完成 BT 校准，随后日志确认进入 IPS；不启动 Wi-Fi 自动连接、TCP 接口或 NAN。控制器回读 8 dBm，Windows 被动扫描确认 22 字节 AD 数据以及非连接/可连接广播类型。采样期间没有 BLE 连接。

AP 和 NP 都进入 PG，并在定时结束后恢复。FreeRTOS 调度期限、500 ms 看门狗及 PMC 休眠日志仍启用。下面各组均为约 60 秒、100 kHz 的整板平均电流，采用 BLU 厂商 SDK 量程切换滤波，保留负噪声样本，不扣减静态基线。

| 广播类型 | 间隔 | 实测 / mA | 官方典型参考 / mA | 高出 |
|---|---:|---:|---:|---:|
| 非连接 | 125 ms | 1.592581 | 1.45 | 9.83% |
| 非连接 | 1 s | 0.433615 | 0.315 | 37.66% |
| 可连接，未连接 | 125 ms | 1.697637 | 1.54 | 10.24% |
| 可连接，未连接 | 1 s | 0.449463 | 0.325 | 38.30% |

停止广播、BT 控制器仍初始化、其余 PG 配置不变的对照均值为 **0.282906 mA**，波形仍有周期性唤醒脉冲。正式四组的绝对差值约 0.119–0.158 mA，可能包含固定系统开销；未进一步分离调度、日志与板级负载。

官方来源为用户提供的 `TR1020_RTL8721F_Power_Consumption_Report_R02(218966).pdf` 第 9 页，§1.6.1 / Table 1-19。报告对应 RTL8721FAF-VA2 等型号、25°C、3.3 V、8 dBm、22 字节、PG，以及禁用 GPIO 以外外设的 IC 典型电流；本次型号标记不同，室温未测量，BLU `Calibrated=0`（校准状态未确认），3.3 V 是设置值，未独立测量板端电压。因此可以确认当前板卡/固件的实测电流高于参考值，不能据此判断 IC 超出规格。

固件 SHA-256：`f9de961a1fc26cbf9ea87145c7c73399169e7b2f097e760b82aad7413485680d`。烧录逐字节回读通过；BLE 测试配置、关闭测试配置的单 Green2 NAN 配置均从独立目录编译和打包通过，构建文件没有引用原 `~/ameba-rtos`。

证据在忽略的 `build/power/green2-com43-blu-20261117/ble-official/`：`comparison.json`、`Green2-BLE-power-comparison.xlsx`、同名 `.png`、`pg-{non,conn}{125,1000}-60s/`、`pg-stopped-60s/`、被动扫描记录及构建/烧录日志。功耗对照结束时恢复 125 ms 非连接广播和 AP 默认防休眠锁；表中电流只对应命令开启 PG 的测量期间。

### PG 广播与连接唤醒

在同一块 COM43 上增加 `blepower wake pg <秒>` 测试模式：进入 PG 前通过 HCI UART RTS 阻止控制器提前发送数据，BT_WAKE_HOST 唤醒后恢复接收。连接成功时持有 `PMU_BT_HOST`；断连并完成重新广播设置后释放。BT 唤醒后保留 2 ms 活跃时间，使串口中断及 HCI 任务处理数据，避免短促反复进入休眠。

修复版连续三轮连接，再补一次延长断连后观察，共四次通过。Windows 被动扫描在 PG 中发现广播，连接后执行样例 GATT 写入/回读，三轮数据分别为 `a5`、`5a`、`c3`，补测为 `a5`。

| 轮次 | 连接/断开时 AP 休眠计数 | 硬件唤醒源 | GATT |
|---|---|---|---|
| 1 | 19 / 19 | WAK_STATUS1=0x100，BT_WAKE_HOST | a5 → a5 |
| 2 | 38 / 38 | WAK_STATUS1=0x100，BT_WAKE_HOST | 5a → 5a |
| 3 | 58 / 58 | WAK_STATUS1=0x100，BT_WAKE_HOST | c3 → c3 |
| 延长观察 | 103 / 103 | WAK_STATUS1=0x100，BT_WAKE_HOST | a5 → a5 |

四次事件的 WAK_STATUS0 都为 `0x4`，PMC timer0 位未置位，连接时的唤醒源为蓝牙；连接期间 PMU 锁为 `0x40`，AP 休眠计数不增长。断连后 AP 计数继续增长，NP 也再次输出 NPPG 日志。无异常、复位或 GATT 读写失败。初版三轮 AP 休眠路径计数为 480 次，修复版为 84 次；测试时长相同，连接耗时受 Windows 主机栈影响，不能把此计数差当成精确的功耗改善比例。

延长观察的整板电流：连接前 PG 广播约 **1.762 mA**，连接保持及 GATT 读写约 **15.110 mA**。断连后的过渡阶段包含较高电流，约 **6 秒**后稳定回到 **1.741 mA**，稳定段观测约 **30.19 秒**。恢复时间由 0.5 秒分箱估算；这些阶段统计不是前述每组一分钟的官方对照数据。BLU 校准状态仍未确认。

修复版固件 SHA-256：`2c89d2f13370677ea0891c22beaa9e8aae6ae787ec3516c48e96d189538179c3`。证据在 `build/power/green2-com43-blu-20261117/ble-wake/` 的 `wake-three-cycles-fixed/`、`wake-long-recovery/`：原始电流、UART/主机事件、`result.json`、`power-report.json` 和 `Green2-BLE-sleep-wake.png`。测试结束后保留此固件、125 ms 可连接广播及 3.3 V 供电，定时窗口已结束，AP 默认 OS 唤醒锁恢复。再次验证可执行 README 中的 `blepower wake pg 300`。

### BLE 自动待机、连接唤醒并启动 NAN

新增默认关闭的 `BLE_NAN_WAKE=ON`，搭配 `NIMBLE=ON NAN=ON NAN_USB=OFF MINIMAL=ON TFM=OFF`。上电完成蓝牙校准后自动广播 `Green2 NAN Wake` 并进入 AP/NP PG；NAN owner task 阻塞等待 BLE 连接，IP/TCP 任务也延迟创建。连接事件持有 OS 唤醒锁，通知同一套 NAN/PASN/TCP 实现启动。BLE 断开不会关闭 NAN；重启重新进入待机。修复 NAN 版无线 IPC 的 BT 电源管理接口改名兼容，构建仍只使用 repo 内文件。

COM43 固件烧录逐字节回读通过。两次独立冷启动均验证：连接前持续广播、APPG/NPPG、无 NAN 初始化；BLE 连接的硬件唤醒源为 `WAK_STATUS0=0x4 / WAK_STATUS1=0x100`（BT_WAKE_HOST）；从固件连接回调到成功发布 NAN 均为 **214 ms**。该时间不包含空口建链和主机发现服务，不能作为精确硬件唤醒延迟。

| 检查 | 结果 |
|---|---|
| 首次冷启动后 PG 连接、GATT | AP 休眠计数 49；a5 写入/回读通过；NAN 仅初始化一次 |
| 第二次冷启动后连接、主机立即断开 | AP 休眠计数 19；NAN 保持 `running=1`，不再广播或进入 PG |
| App 连接 | 使用已有配对完成 PASN type=2、NDP、TCP 8000 建链 |
| App 上传，10 秒 | 15,663,104 字节 / 10,077 ms，**12.434 Mbps** |
| App 下载，10 秒 | 23,789,568 字节 / 10,016 ms，**19.001 Mbps** |
| 软件错误/丢弃计数 | `err=0 dropped=0 rxdrop=0 txdrop=0` |
| 构建回归 | 新 BLE+NAN 模式、普通单 Green2 NAN、原 BLE_POWER_TEST 均编译打包通过 |

“主机立即断开”指 Windows 的连接与服务发现返回后立即断开；NAN 已在固件回调后启动，不宣称覆盖了 214 ms 初始化窗口内断连的竞态。首轮脚本尝试在 PG 期间查询串口状态超时，因 LOGUART RX 时钟被门控；随后改用 PMC 日志和被动 BLE 扫描验证待机，BLE 唤醒后控制台恢复并通过状态检查。

BLU COM17 在 3.3 V 设置下连续采样约 59.991 秒，厂商 SDK 换算得到以下整板阶段均值。边界按主机 UART 时间对齐并留出过渡余量，`Calibrated=0`，校准状态未确认；此轮仅测 NAN 开启前后，不含后续 App 跑流。

| 状态 | 统计时长 / s | 电流 / mA |
|---|---:|---:|
| PG 待机，125 ms BLE 广播 | 14.490 | 1.761534 |
| BLE 连接保持，NAN 已开启 | 11.334 | 48.860791 |
| BLE 断开后，NAN 持续开启 | 23.753 | 49.109791 |

固件 SHA-256：`f81f334c8c4c42dce1a3b66d094fb8010b2bbcd209030f463f478f94f83796ca`。证据在 `build/power/green2-com43-blu-20261117/ble-nan-wake/`：`cold-boot-2/`、`cold-boot-3/`、`app-test/`、构建/烧录日志、`validation.json`、源码快照及 `cold-boot-2/Green2-BLE-wake-NAN.png`。测试后维持 3.3 V 供电，设备处于 NAN 活动状态；未写 eFuse，烧录保留了已有配对，zlib 未修改。

### 2026-11-17～18：暂停 NAN，BLE 唤醒后保留主机状态恢复

在 `BLE_NAN_WAKE=ON` 中新增 `blenan sleep`。NAN 已开启且 BLE 已断开时，命令在 NAN owner task 中结束当前数据连接、停止 NAN 并移除无线接口，使 WLAN 进入 IPS，随后恢复 125 ms BLE 广播和 AP/NP PG。IP 栈、TCP 监听任务、PASN 缓存对象和 RAM 中的配对身份保留；旧会话授权及临时密钥清除。BLE 连接后只重建无线接口、调度和发布，不重复完整 Wi-Fi/IPC 启动、P-256 自检、PASN 初始化、配对文件读取及 IP/TCP 任务创建。

只对现有 NAN 执行 AP SleepCG 时，22 秒内 AP 进入 459 次，电流仍约 48.937 mA，NP 没有进入 PG。进一步验证，仅执行 NAN STOP 虽可在 16 ms 恢复，但无线接口仍存在，阻止驱动进入 IPS。最终实现必须在暂停时 DEINIT 无线接口、恢复时 INIT；因此这里保留的是主机协议栈和配对，不保留 NAN 空口在线状态或原 NDP/TCP 连接。

COM43 最终固件逐字节烧录回读通过。连续两次暂停/恢复均通过被动 BLE 扫描、连接及 GATT 读写；暂停期间分别记录 49、18 次 NPPG。恢复耗时两次均为 **37 ms**，定义为固件 BLE 连接回调到 NAN 发布完成，不包含手机发现、空口建链或 App 重连耗时。最终 `host_inits=1 radio_inits=3 pauses=2 resumes=2`；完整 NAN 初始化、P-256 自检和 TCP 监听任务创建在记录中各出现一次。

BLU COM17、3.3 V 设置值下连续采样 59.996 秒，以下为去除过渡区后的整板阶段均值。厂商 SDK 换算及量程切换滤波；`Calibrated=0`，校准状态未确认，板端电压未独立测量。原始数据 5,999,616 点，无非法量程或尾部残缺；协议无帧序号，不能据此证明完全没有丢样。暂停到最低稳定电流存在数秒过渡，下面待机值不包含该过程。

| 状态 | 统计时长 / s | 电流 / mA |
|---|---:|---:|
| NAN 已开启、未跑流 | 3.540 | 49.203693 |
| 暂停 NAN，仅 BLE 广播，AP/NP PG | 15.894 | 1.715448 |
| BLE 唤醒后 NAN 恢复、未跑流 | 27.563 | 49.057653 |

用户随后确认 App 重连及双向各 10 秒测速完成；UART 记录已有配对认证、NDP 与 TCP 建链，结果如下。此轮 App 跑流不在上表功耗采样窗口内。

| 验证 | 结果 |
|---|---|
| 上传 | 15,400,960 字节 / 10,092 ms，**12.208 Mbps** |
| 下载 | 25,755,648 字节 / 10,008 ms，**20.588 Mbps** |
| 最终计数 | `up=1 down=1 err=0 dropped=0 rxdrop=0 txdrop=0` |
| 保留状态 | App 测速后仍为 `host_inits=1 radio_inits=3 pauses=2 resumes=2` |
| 主机测试 | 6/6 ASan/UBSan/LeakSanitizer 测试通过，包含暂停撤销旧授权、恢复不读取配对文件及完整停止后拒绝保留恢复 |
| 构建回归 | BLE_NAN_WAKE 固件与未启用 BLE 的普通单 Green2 NAN 均编译打包通过；构建文件无原 `~/ameba-rtos` 引用 |

LeakSanitizer 在沙箱 ptrace 限制下无法运行，随后在沙箱外执行同一组测试并全部通过。两次暂停测试发生在 NAN 开启但尚未建立 App 数据连接时，覆盖暂停后重新建链；尚未覆盖持续跑流中途强制暂停的压力测试或长时间重复休眠。

最终固件 SHA-256：`3db8fcd55bf8e0a212c067f71e5cf5ed5009310c83d576976103ea1b72092060`。证据在 `build/power/green2-com43-blu-20261117/nan-retain/`：`existing-cg/`、`resume-1/`（STOP-only 未达到 NP PG）、`resume-radio-off/`、`app-test/`、构建/烧录日志、`validation.json` 和源码快照。功耗图为 `resume-radio-off/Green2-NAN-retain.png`。测试结束时 COM43 保持 NAN 活动、BLE 已断开，3.3 V 供电保持；本轮未写 eFuse，zlib 未修改。

### 2026-11-18：传输完成后自动休眠

`BLE_NAN_WAKE=ON` 中增加当前 NAN TCP 服务的自动待机：一次上传/下载成功后开始 5 秒空闲计时，收到下一条命令首字节即取消旧计时；未完成的命令和数据传输仍按协议期限处理。空闲到期、发送缓冲清空后关闭客户端，由同一个 NAN owner task 执行暂停/PG。监听任务仅在计时期间每 100 ms 检查，待机后恢复阻塞，避免额外唤醒。NDP 新连接和休眠操作也清除旧计时，恢复后不会立刻再次休眠。兼容 FreeRTOS `accept()` 超时返回 NULL 的行为。

COM43 修复版烧录逐字节回读通过。用户完成 App 双向各 10 秒测速，已有配对认证、NDP、TCP 正常；第一方向到第二方向之间没有休眠。最后一方向成功后未发送手动休眠命令，设备自动暂停 NAN 并持续 BLE 广播，记录到 93 次 NPPG。

| 从最后一笔传输完成起算 | 时间 |
|---|---:|
| 发起自动休眠 | **5.016 s**，固件空闲计时器 |
| 首次 AP PG | 约 **5.13 s** |
| 首次 NP PG，此时双核均已进入 PG | 约 **5.32 s** |
| 电流进入稳定低功耗段 | 约 **11.3 s** |

传输完成以固件结果日志为起点，不是手机 UI 时刻。PG 时间使用 UART 主机接收时间；电流稳定判据为连续六个 0.5 秒均值低于 3 mA，取第一个分箱起点，时间分辨率约 0.5 秒。前 5 秒是有意保留的应用空闲窗口，此后还有约 6 秒电流过渡；没有把第一次 PG 日志等同于最低稳态电流。

BLU COM17 在 3.3 V 设置下采样 59.991 秒，厂商 SDK 换算/滤波所得整板阶段均值如下。`Calibrated=0`，校准状态未确认，板端电压未独立测量。5,999,104 个样本中无非法量程或残缺尾部，协议无帧计数，不能证明零丢样。

| 状态 | 统计时长 / s | 平均电流 / mA |
|---|---:|---:|
| 传输完成后等待自动休眠 | 4.536 | 51.370123 |
| 双核 PG，仅 BLE 广播稳态 | 36.791 | **1.742936** |

| 传输及恢复验证 | 结果 |
|---|---|
| 上传 | 15,859,712 字节 / 10,090 ms，**12.574 Mbps** |
| 下载 | 24,379,392 字节 / 10,021 ms，**19.462 Mbps** |
| 错误及丢弃 | `up=1 down=1 err=0 dropped=0 rxdrop=0 txdrop=0` |
| 自动休眠后再次 BLE 唤醒 | 被动扫描、连接、GATT 5a 写入/回读通过；NAN **37 ms** 恢复 |
| 初始化计数 | `host_inits=1 radio_inits=2 pauses=1 resumes=1`；完整初始化和 TCP 监听创建各一次 |
| 旧计时清除 | 再次唤醒并断开 BLE 后等待超过 7 秒，仍保持 NAN 活动，没有被上一笔传输的计时重复休眠 |
| 构建回归 | BLE_NAN_WAKE 与普通单 Green2 NAN 均编译打包通过，构建文件无原 SDK 引用 |

此轮覆盖一次真实 App 的完整自动休眠/再次唤醒循环，不覆盖持续 BLE 连接、慢客户端重传及长时间重复休眠的故障注入测试。再次唤醒验证完成后，额外手动返回 BLE 广播 PG 待机，保持 3.3 V 供电，等待下一次 BLE 连接；这一收尾操作不计作第二次自动休眠。

最终固件 SHA-256：`87ff0a5329cb955ede6a068388216ac555827ea091e4d2f5f94a8d6cf9e61737`。证据在 `build/power/green2-com43-blu-20261117/nan-auto-sleep/`：`app-cycle-fixed/` 下的原始电流、UART/事件、`result.json`、`power-report.json` 和 `Green2-transfer-to-sleep.png`，以及构建/烧录日志、`final-standby.json`、`validation.json` 和源码快照。未写 eFuse，zlib 未修改。

### 2026-11-18：NAN 传输后 SoC DSLP（App 传输循环待完成）

增加独立的 `NAN_DEEP_SLEEP=ON` 配置，与 BLE 广播/唤醒配置互斥。复用 TCP 成功传输后的 5 秒空闲检测和 NAN owner task 的无线停止流程；随后释放 AP deep wakelock，通知 NP 执行 `SOCPS_DeepSleep_RAM()`。唤醒会重新经过 ROM、bootloader 和应用初始化，从 LittleFS 加载配对。该模式关闭 BLE，支持 AON 定时唤醒和 BLU 断电再上电启动，不能通过 BLE 连接唤醒。

手动命令 `nan sleep 10` 按 SDK 标称频率设置 AON 定时器；`nan sleep 0` 禁用定时唤醒。修正 NP 对 `PMU_SLEEP_FOREVER` 的处理：先清除/禁用旧 AON 中断，仅在有限时长时重新启用，避免把无限等待值乘以 100 后溢出为有限计数。关闭其他子系统的 wakelock 不属于本模式的操作。

COM43、RTL8711FAF-VA2-CG、BLU COM17、3.3 V 设置值下的预检结果如下。供电保持开启时观测到电流下降和 AON 自主唤醒；两个启动均为 `BOOT REASON=0x2`、`AON=0x8`，确认进入 DSLP。BLU Off/On 测试启动原因为 `0x0`；这验证供电重启，不代表已测试 CHIP_EN 或外部电源锁存关机电路。

| 预检 | 第一轮 | 第二轮 |
|---|---:|---:|
| 定时设置 | 10 s | 10 s |
| 电流波形中的深睡时长 | 14.25 s | 14.23 s |
| 深睡稳态平均电流 | 8.21 µA | 7.77 µA |
| 稳态电流统计窗口 | 13.45 s | 13.43 s |
| 电流上升到 NAN 发布完成 | 296 ms | 278 ms |
| 应用 NAN 初始化计时 | 227 ms | 227 ms |

预检原始采样 4,499,456 点、44.99456 s，厂商 SDK 换算/量程切换滤波，无非法量程或残缺尾部；协议无帧计数，不能证明零丢样。`Calibrated=0`，仪器校准状态未确认，板端电压未独立测量。电流边界按 10 ms 分箱定位；UART 使用主机接收时间，不等同于精确芯片复位延迟。预检固件 SHA-256 为 `6a21fcc3ab25070ee368f3e7b3f1c3f8d4ba8d2c17133d378675498633b36e04`。

AON 实际时长偏离设置值。用活动状态时钟测频换算计数的试验仍得到 13.96 s，未解决深睡定时误差，因此最终代码保留 SDK 标称计数方法，未加入板级经验补偿、振荡器 trim 或 eFuse 修改。这项精确定时限制仍待进一步定位。

最终测试固件 `02b97900e5362f5f9b5335f35f940d85c442c4f19e3fbf4fcdf185dbf4525b26` 已逐字节烧录回读通过，并再次验证从 AON DSLP 唤醒，NAN 应用初始化 228 ms，计数为 `host_inits=1 radio_inits=1 pauses=0 resumes=0`。已有配对认证 `type=2`、NDP 和 TCP PONG 已通过。普通单 Green2 NAN 与 BLE_NAN_WAKE 配置均编译打包通过。

本轮采集等待 App 上传/下载 20 分钟后超时退出，串口记录中只有配对认证和 TCP PONG，没有 throughput 结果，未启动传输阶段的电流采样。记录结束时 NAN 仍在运行；这不构成完整 DSLP 传输循环通过，也不能用旧 PG 模式的结果代替。后续仍需 App 双向各 10 秒测速，确认自动进入 DSLP、再次 AON 唤醒，并补充实际耗时和电流。`nan sleep 0` 的最终实机停留检查也尚未执行。

证据目录：`build/power/green2-com43-blu-20261117/nan-deep-sleep/`，含 `preflight/`、`preflight-calibrated/`、`app-cycle-final/`、最终构建/烧录日志、`build-manifest.json` 和 `source.patch`。本轮未写 eFuse；zlib 的原有改动未处理。

### 2026-11-18：COM5 深睡编译及手动唤醒复测

在 `test/bl618dg-native-whc-usb` 的 `0232348f` 上重新构建，使用独立目录，避免沿用其他配置的产物：

```sh
make -C project/amebagreen2 BUILD="$PWD/build/green2-nan-deep-20261118" \
  NAN=ON NAN_DEEP_SLEEP=ON NAN_DEEP_SLEEP_MS=30000 \
  NIMBLE=OFF BLE_NAN_WAKE=OFF BLE_POWER_TEST=OFF NAN_USB=OFF \
  MINIMAL=ON IPERF=OFF TFM=OFF image
```

双核固件编译打包通过，NAN 本地 ASan/UBSan 测试 **6/6 通过**。确认 COM5 是已连接的 CH340/Green2，串口可读且 NAN 正在运行后，使用同端口 DTR/RTS 烧录。`firmware.bin` 为 897,824 字节，SHA-256 为 `ff1b90cf5d70f3c95bc16ba7922471f029a99a5d0ea4602aa737a1d8cc327801`，逐字节读回通过；只写固件，保留 LittleFS 和配对存储。

| 实机检查 | 结果 |
|---|---|
| `nan sleep 3` | AON 唤醒，启动原因 `0x2`、AON `0x8`；进入日志至启动原因日志 4.531 s，至 NAN 活动日志 4.760 s；NAN 初始化 228 ms |
| `nan sleep 10` | AON 唤醒，启动原因 `0x2`、AON `0x8`；进入日志至启动原因日志 14.756 s，至 NAN 活动日志 14.987 s；NAN 初始化 226 ms |
| 有限定时测试后执行 `nan sleep 0` | 观察 60 s，无自主启动、`entry pending` 或故障日志 |
| 无定时唤醒状态下 RTS 复位 | 启动原因 `0x0`、AON `0x0`，NAN 初始化 229 ms，`running=1` |

上述时间使用主机串口接收时间，包含进入前 100 ms 的串口排空及启动开销，不等同于电流波形测出的深睡时长。AON 定时偏差仍存在，未修改计数算法或 trim。本机没有 COM17 电流仪，没有测量这一块 COM5 板的深睡电流，也未以另一台架的电流结果替代。

MacBook 可通过 SSH 访问，但 CoreDevice 显示 iPhone 13 Pro 不在线；本轮尚未验证 App 双向测速后的自动 DSLP、AON 唤醒后的保存配对重连。手动测试未出现 HardFault、断言、内存分配失败或栈溢出。结束时模组保持 NAN 活动，COM5 已释放；构建生成的 zlib `zconf.h` 改动已还原。

脱敏结果：[deep-sleep-com5-validation.json](../../component/amebagreen2/nan/tests/results/20261118/deep-sleep-com5-validation.json)。原始构建、烧录及串口证据保存在 `build/green2-nan-deep-20261118/test/`，原始 UART 可能包含密钥，不应公开。

同日继续自动深睡测试：iPhone 上线，App 为回退到 BLE 配对功能之前的 `1.0 (8.1)`。三次自动测速均未完成服务发现，无传输结果，也没有触发自动深睡。复位后板端收到订阅报文，未出现崩溃或队列丢失。最后一次人工重新配对完成 SAE/PASN setup，但板端连续五次 `cache rc=-1`，随后重连认证失败。

检查发现本分支尚未包含 `amebad-aws-ota` 的缓存修复：服务实例号可以被不同设备或重新配对复用，不能单独作为 NPK 绑定依据；普通 PMKID/cache 刷新也不应因此拒绝。补入成功 SAE 凭据匹配和持久化成功后再提交内存、清除待存密钥的处理。新增回归先复现原实现失败，修复后本地 ASan/UBSan **6/6 通过**，保留已有 pause/resume 验证。此时用户要求将所有 NAN 分支合并至 `master`；修复尚未烧录，自动深睡及唤醒重连仍未通过实机验证。续测结果见 [automatic-deep-sleep-attempts.json](../../component/amebagreen2/nan/tests/results/20261118/automatic-deep-sleep-attempts.json)。

### 2026-11-18：NAN 分支合并验证

将 `amebad-aws-ota@2da5d22` 和 `test/bl618dg-native-whc-usb@744a87c` 合并到
`master`，保留 BLE OOB、配对缓存修复、MACID/NDC 清理、共享 USB 主机协议栈、
BLE 唤醒和整片深睡配置。BLE OOB 与 standby 使用独立内部事件编号；不兼容的
BLE 应用、USB 桥接和休眠配置在 CMake 配置阶段拒绝。

合并后的 NAN ASan/UBSan 测试 **14/14 通过**，另通过原生 WHC 报文编解码
（含 100000 个畸形帧）及配对存储断电恢复测试。四组 OOB 与功耗/USB 互斥配置
均按预期拒绝。Green2 的独立 NAN、`NAN_DEEP_SLEEP`、`NAN_BLE_OOB`、
`BLE_NAN_WAKE` 和 `NAN_USB` 五种配置均完成双核编译打包；ELF 符号检查确认
各自功能及 NP NDC 清理包装函数已链接。BL618DG 的原生 WHC USB 与自定义桥接
两种主机使用 Zephyr SDK 1.0.1（GCC 14.3.0）编译通过。

本次合并验证未烧录硬件，COM5 仍为前述 `ff1b90cf` 固件；自动深睡及唤醒重连
仍待烧录修复版后实测。结果及镜像摘要见
[master-merge-validation.json](../../component/amebagreen2/nan/tests/results/20261118/master-merge-validation.json)，
完整构建日志保留在 `build/nan-merge-master-20261118/`。

### 2026-11-18：合并后 COM5 自动深睡、保存配对和唤醒重连

将上述合并版烧录到 COM5 后，重新配对的 SAE 握手成功，但缓存仍报身份匹配
歧义：历史记录复用了服务实例号和固定 PIN，凭据哈希不能单独区分新旧 NIK。
修复为接收已认证 peer NMI 对应的 PMKID 更新，再用候选 NIK 校验该 PMKID 的
NIRA 标签，并同时检查已完成握手的角色、实例号和凭据。无线库 event 31 的
实际 ABI 为 **41 字节**，包含旧 PMKID，成功状态为 `1`；公开头文件仍是缺少
旧 PMKID 的 25 字节布局。无匹配或重复身份仍拒绝绑定，持久化失败保留待存
NPK 供重试。本地 ASan/UBSan **14/14 通过**，覆盖相同实例和 PIN 的不同身份、
错误 peer/角色/状态、截断事件、伪造 NIRA、重复身份及写入失败。

最终固件基于 `master@0b7d8d1` 加上述本地修复，899584 字节，SHA-256：
`3fef5e8099dc299d8adf2c15fef34b237fc3ebdff03a3a9f7b90ef525dffa4cb`。
配置为 `NAN=ON NAN_DEEP_SLEEP=ON NAN_DEEP_SLEEP_MS=30000 MINIMAL=ON`，
NimBLE、NAN BLE OOB、BLE 唤醒、USB、IPERF 和 TF-M 均关闭。COM5 同口
DTR/RTS 自动下载、逐字节回读及启动检查通过；保留 LittleFS、配对和校准分区，
未写 eFuse。用户重新配对后记录 `authenticated pairing persisted slot=2`，
三次 cache 更新均为 `rc=0`，无拒绝。

旧 App 1.0 (8.1) 的首轮传输、自动深睡、唤醒和保存密钥认证已成功，但压力测试
随 PING 一起发送 FIN，FreeRTOS+TCP 进入 `CLOSE_WAIT` 后拒绝发送 PONG。
在 BLE 配对功能加入前的 App `9ed3b1e` 上恢复“收到 PONG 前保持发送方向打开”
的处理，签名构建、签名验证和安装通过，测试版本为 **1.0 (8.2)**。没有恢复
NAN BLE 配对功能。安装后一次启动因手机锁屏被系统拒绝，不计为协议测试；
用户解锁后完成下面两轮连续循环。

| 检查 | 第 1 轮 | 第 2 轮 |
|---|---:|---:|
| 上传 10 秒，双方字节数一致 | 15,466,496 B / **12.291 Mbps** | 16,056,320 B / **12.719 Mbps** |
| 下载 10 秒，双方字节数一致 | 26,017,792 B / **20.800 Mbps** | 26,083,328 B / **20.797 Mbps** |
| 测速开始至 App ready | 2.483 s | 2.008 s |
| 传输完成后自动 standby 空闲计时 | 5032 ms | 5036 ms |
| 进入 DSLP 日志至唤醒原因日志 | 44.061 s | 44.007 s |
| 唤醒后固件 NAN 初始化 | 228 ms | 228 ms |
| 唤醒后 App 发现及重连 ready | **3.456 s** | **3.449 s** |
| 保存配对认证、TCP PONG | 通过 | 通过 |
| PING 后空闲 7 秒，不再次触发深睡 | 通过 | 通过 |

第 1 轮开始前执行一次 RTS 复位，第 2 轮直接接续；两轮各一次自动 DSLP，
均以 `BOOT=0x2 / AON=0x8` 唤醒，过程中无需重新配对。各轮传输前和唤醒后
均有 PASN type=2 成功记录。未见 HardFault、断言、内存分配失败、MACID 耗尽、
`staadd fail`、缓存拒绝或 TCP 协议中止。唤醒后的最终状态均为
`running=1 pong=1 err=0 dropped=0 rxdrop=0 txdrop=0`；这些计数在唤醒重启后
重新累计，不能用来代表此前整段传输。

两轮 AP 最低堆采样为 29216 / 30336 字节，NP 为 50712 / 53232 字节；均为
周期采样值，不是实际最低水位。MACID 占用采样峰值为 3/12 和 2/12。
本次仅验证两轮完整循环，未重新进行长时间 MACID/NP 内存压力测试。

**30 秒 AON 设置对应约 44 秒 UART 间隔的偏差仍存在**，未修改时钟校准或
补偿。该间隔含串口排空和启动，不等同于电气测量的深睡时长。COM5 此轮
没有接功耗仪，不能引用其他板卡电流作为本板结果。

脱敏结果和源码摘要见
[deep-sleep-master-hardware.json](../../component/amebagreen2/nan/tests/results/20261118/deep-sleep-master-hardware.json)。
原始日志留在忽略目录 `build/nan-merge-master-20261118/hardware/`，可能包含
配对密钥，不公开。该轮验证完成时，固件和 App 修复尚未提交或推送。

### 2026-11-18：NAN 上传接收路径排查（优化版吞吐待测）

在上述 `3fef5e80` 固件和 App 1.0 (8.2) 上重新测得上传 **12.704 Mbps**、
下载 **20.635 Mbps**，每方向 10 秒，双方字节数一致，随后自动深睡和 AON
唤醒通过。未见软件 RX/TX 丢包、内存分配失败、MACID 耗尽或协议中止。

诊断发现较大的应用核接收路径开销：一轮 10.041 秒的上传处理 13,707 个
接收包，驱动准备累计 1.206 秒、交给网络栈累计 4.847 秒。这里是调用区间
耗时，包含高优先级 IP 任务抢占，不能作为纯 CPU 利用率；分层计时也不能
相加。计时本身会降低测速结果。将 ACK 提前到八个 MSS 的试验仅得到
11.904 Mbps，对照诊断版为 11.534 Mbps，没有明显改善，已撤回。

接收优化版移除 NAN 的中间 pbuf 分配及整包拷贝，直接复制到 FreeRTOS
网络缓冲；使用 linked RX messages 合并现有队列里的包，每批最多 8 包，
队列为空立即提交，不增加等待定时器。NDP 代次失效或事件队列满时释放整批
缓冲。改动只作用于 Green2 本地 NAN，其他接口和 USB 桥接保留原路径。

优化版 `firmware.bin` 为 **899872 字节**，SHA-256：
`33af6829b5c7999a69009f1a11414711c52aae1997980fef2874fe8fe36ffc17`。
配置继续使用 NAN 自动深睡 30000 ms，`NAN_NET_DIAG=OFF`；App 未修改。
COM5 烧录、逐字节读回及 NAN 启动检查通过；保留配对和校准分区，串口已释放。
原生 ASan/UBSan **15/15 通过**，新增缓冲所有权测试覆盖立即抢占、批次上限、
队列满、链路失效及错误后的复用；NAN 开启、关闭两种固件构建通过。

**接收优化版尚无手机吞吐数据，也尚未验证其传输后的自动深睡和重连。**
iPhone 在下一轮测试前变为 CoreDevice `unavailable`，已请求恢复 USB 连接和
前台状态。不能把旧固件的测量或诊断版结果当作优化后的提升。
脱敏报告见 [upload-receiver-analysis.json](../../component/amebagreen2/nan/tests/results/20261118/upload-receiver-analysis.json)，
私有构建及原始日志位于 `build/green2-upload-20261118/`。

### 2026-11-18：master 接收优化版 COM43 吞吐及自动 DSLP 补测

拉取并切换到 master `91cb2e69318b23f893173949756bff5f55718169`，在
COM43 的 RTL8711FAF-VA2-CG 上重新构建、烧录并逐字节回读通过。配置为
`NAN=ON NAN_DEEP_SLEEP=ON NAN_DEEP_SLEEP_MS=30000 MINIMAL=ON`，
NimBLE、BLE OOB、USB 桥接、TF-M 及网络/堆诊断均关闭。BLU COM17 保持
3.3 V 设置供电；原有配对、校准分区保留，未写 eFuse。固件 900032 字节，
SHA-256：`9493035a39abdc438e7f52f1b1f09e9313760a40464cc98986f42269277ee237`。
原生 ASan/UBSan/LeakSanitizer **15/15 通过**，构建配置无原 SDK 路径引用。

用户最初报告输入固定 PIN 后配对失败，该时间窗口的 COM43 日志没有 PASN
认证请求。未修改固件或清除配对，用户重试后 `type=1` 认证通过，配对保存到
slot 1、`cache rc=0`，随后 `type=2` 验证和数据建链成功。首次失败原因尚未
确定，不能将重试成功描述成已修复某个配对缺陷。

| 轮次 | 方向 | 有效负载字节 | 固件计时 | 吞吐量 |
|---|---|---:|---:|---:|
| 1 | 上传，App → Green2 | 21,889,024 | 10,052 ms | **17.420 Mbps** |
| 1 | 下载，Green2 → App | 28,377,088 | 10,017 ms | **22.663 Mbps** |
| 2 | 上传，App → Green2 | 21,626,880 | 10,039 ms | **17.234 Mbps** |
| 2 | 下载，Green2 → App | 28,377,088 | 10,003 ms | **22.694 Mbps** |

两轮结束时均为 `up=1 down=1 err=0 dropped=0 rxdrop=0 txdrop=0`。最后一笔
传输结束后分别 **5.029 s / 5.016 s** 自动发起待机，随后进入 DSLP；两次
AON 唤醒的启动原因为 `0x2`、唤醒源为 `0x8`，NAN 应用初始化均为
**231 ms**，恢复后计数为
`host_inits=1 radio_inits=1 pauses=0 resumes=0`。未发送手动休眠、复位或
断电指令触发此循环。

主机接收时间中，两轮 DSLP 入口日志到 ROM 启动日志分别为
**43.000 s / 43.016 s**，到 NAN 发布完成为 **43.219 s / 43.235 s**；
这些数值包含串口排空和启动过程，不等于电流波形
中的纯深睡时长。本轮未采集电流，也不据此重新宣称此前约 8 µA 的结果。

两轮完整上传/下载及自动 DSLP/AON 循环通过。第二轮在第一次真正深睡唤醒后
使用已保存配对，日志仅出现 `PASN authenticated type=2`，没有再次执行
`type=1` 新配对。服务重启使采集进程在第一轮唤醒后退出，原始记录保留；
之后重新打开 COM43，未改变固件或设备供电，第二轮完整采集及检查通过。
结束时 NAN 已再次恢复，串口采集正常关闭，3.3 V 供电保持开启。

两轮数据不足以证明长期稳定性；本次没有同时进行同板旧版固件对照，不能将
相对历史速率的全部差异归因于单项接收优化。

脱敏结果见 [master-com43-rx-throughput.json](../../component/amebagreen2/nan/tests/results/20261118/master-com43-rx-throughput.json)。
构建、烧录、原生测试与原始串口记录位于 `build/power/green2-master-throughput-20261118/`。
zlib 的 HEAD、索引差异和工作区状态摘要与拉取前一致。

## 2026-11-18：COM5 NAN 功率状态读取

在 `master@e3e2676` 上新增默认关闭的 `NAN_RADIO_DIAG=ON`。NP 在切信道后
保存只读快照，由现有周期任务每 10 秒打印，避免在切信道或逐包路径打印。
私有 RF 字段对应已锁定 SHA-256 的 NAN 驱动库；没有修改功率表、国家码、
TPC 开关或调度参数。固件为 900,768 字节，SHA-256 为
`3b1227bc554c81470211dce84ab1d833d9ac114f4dde21986f40d2f8b6d56388`。

COM5 固件烧录、逐字节读回、应用启动及一次 `nan stop` / `nan start` 均通过，
运行告警检查未见 fault、分配失败或栈溢出。诊断完整构建、单文件
`-Wall -Wextra -Werror` 编译、关闭诊断的 NP 构建通过；关闭后无采样钩子及
诊断符号。开启诊断的 NP text 增加 896 字节，BSS 增加 256 字节。

启动应用国家码 CN。读取到 `table=2 efuse_sel=1 byrate=1 limit=1`，
没有强制功率覆盖，TPC 配置及活动状态均为 0。空闲发现期间采样到 ch6 和
ch149，两者均为 20 MHz。下表为驱动返回值除以 4 后的目标功率：

| 查询速率 | ch6 / 2.4 GHz | ch149 / 5 GHz |
| --- | ---: | ---: |
| OFDM 6M | 15 dBm | 18 dBm |
| OFDM 54M | 15 dBm | 17 dBm |
| HT / HE MCS0 | 15 dBm | 18 dBm |
| HT / HE MCS7 | 15 dBm | 16 dBm |
| HE MCS8 | 15 dBm | 15 dBm |
| HE MCS9 | 15 dBm | 14 dBm |

按用户要求仅测试板端。以上是指定速率的驱动目标，未使用仪表测量实际射频
功率，也不代表正在使用这些 PHY 速率。无手机 peer，不能据此认定 NDP 已
协商到 5 GHz；本轮没有重测传输、配对、深睡或启用 TPC 的行为。
结束时 COM5 保留诊断固件、NAN 活动，串口已关闭，已有配对存储保留。

脱敏结果见 [radio-power-com5.json](../../component/amebagreen2/nan/tests/results/20261118/radio-power-com5.json)，
原始日志位于 `build/green2-rf-20261118/`。构建生成的 zlib `zconf.h` 改动已还原。

## 2026-11-18：COM5 NAN 固定 8 dBm 目标功率

按用户要求，在每次 `green2_nan_radio_start()` 进入发现前调用
`wifi_set_tx_power()`，设置 `tx_pwr_force=32`（0.25 dBm 单位）并开启强制覆盖。
设置失败时返回错误，不继续启动 NAN。该设置作用于共享 RF 的所有速率与频段，
不修改校准数据或 eFuse；驱动在此模式下覆盖速率/限制表，报告 `limit=0`。

双核完整构建、修改的 transport 和诊断文件 `-Wall -Wextra -Werror` 编译、
COM5 固件烧录及逐字节读回均通过。固件为 901,024 字节，SHA-256 为
`bdee3d8e0b406d6bc9d3e14861340ee7bd738f44563e06a0ca1abd4be39c6b43`。
配置沿用 NAN、深睡 30000 ms、MINIMAL，并开启只读 RF 诊断；BLE、IPERF、
TF-M 关闭，保留 LittleFS 配对与校准分区。

启动后、一次 `nan stop` / `nan start` 后、一次 `nan sleep 2` 真实 DSLP/AON
唤醒后，ch6 和 ch149 均读到 `force=1 force_qdbm=32`。OFDM6/54、HT0/7、
HE0/7/8/9 的目标值全部为 32，即 **8 dBm**，读取成功掩码均为 `0xff`。
唤醒原因为 BOOT=0x2、AON=0x8；重新启动成功，默认下次深睡时间恢复为
30000 ms。未见 fault、分配失败、栈溢出或 MACID 耗尽；结束时 NAN 活动、
零 peer，串口已释放。

这验证的是软件设置及驱动目标读回，没有仪表校准实际射频输出。本次仅测板端，
没有手机建链、吞吐或自动传输后深睡测试。
脱敏结果见 [radio-power8-com5.json](../../component/amebagreen2/nan/tests/results/20261118/radio-power8-com5.json)，
私有构建和日志位于 `build/green2-rf-20261118/power8*`。

## 2026-11-18：COM15 / BLU 3.3 V、8 dBm 板端功耗

拉取 `257f321` 后使用新增的 `blu-power-monitor` skill。本机 BLU 为 COM10
（VID/PID `15A2:300A`），COM15 的 DTR 接 Green2 LOG_TX；用户确认板卡仅由
BLU 供电。先关闭输出并缓存本机校准参数，设置/读回 3300 mV，再用 LOG_TX
下载电平和 BLU 上电进入烧录。复用上一节同 SHA-256 的 8 dBm 诊断固件，
逐字节回读通过。该板原有 LittleFS 无法挂载，用户明确批准后执行 `mkfs -y`
成功；重新上电后 NAN 初始化正常。没有写校准分区或 eFuse。

首次会话目录放在 WSL UNC 路径，发生状态文件重命名失败；未重放未确认的
On 请求。迁到 Windows 本地目录后，通过明确 Off/Voltage/On 恢复，后续
采集正常。使用用户提供的厂商 SDK（SHA-256
`f94da4e9be6447d1a5132dfcefda912b5b015310117e3aa2691eae6e018c35d6`）
`get_samples()` 换算并保留量程切换滤波、负噪声样本，不扣静态基线。

| 状态 / 统计窗口 | 平均电流 | 按 3.3 V 计算的功耗 |
| --- | ---: | ---: |
| NAN 发现，三个活动窗口合计 26.123 s | 51.723 mA | 170.685 mW |
| 10 s 定时，深睡第 1 轮，剔除过渡区后 13.170 s | 10.198 µA | 33.652 µW |
| 10 s 定时，深睡第 2 轮，剔除过渡区后 13.138 s | 9.285 µA | 30.639 µW |
| 30 s 定时，深睡区间 42.185 s | 8.686 µA | 28.663 µW |
| 30 s 定时，休眠末段 10 s | 7.678 µA | 25.338 µW |

两次 60 秒采样分别包含 5,999,616 点，100 kHz，均无非法量程或残缺尾部。
两轮短休眠电流仍缓慢下降，因此另外测默认 30 s 设置，并单列末段窗口，
不将短窗口均值当作完全稳定值。电流波形低于 1 mA 的连续区间（10 ms 分箱）
分别为 14.59、14.56、43.62 s，AON 定时偏差仍存在。三次 BOOT=0x2 / AON=0x8，
NAN 应用初始化 226、227、227 ms，唤醒后仍为 8 dBm。

以上为当前板卡与接线的供电电流；BLU `Calibrated=0`，校准状态未确认，
3.3 V 是设定/读回值，未独立测量板端电压。采样协议没有帧序号，不能由格式
检查宣称零丢样。这里是手动深睡测试，不含手机跑流或自动传输后休眠。
用户随后完成 COM15 首次配对。保持同一固件、3.3 V 设置，iPhone 13 Pro
分别上传/下载约 10 s，接收字节与发送字节一致，保存配对 PASN type=2 成功。

| 8 dBm 目标功率跑流 | 吞吐量（接收端） | 平均电流 | 按 3.3 V 计算的功耗 |
| --- | ---: | ---: | ---: |
| 手机 → Green2（上传） | 15.633 Mbps | 64.742 mA | 213.649 mW |
| Green2 → 手机（下载） | 21.335 Mbps | 124.686 mA | 411.462 mW |
| 自动深睡末段 10 s | — | 8.331 µA | 27.493 µW |

跑流电流使用 UART 完成时刻减固件耗时定位，每端剔除 0.5 s，统计窗分别
9.061/9.011 s。两段标准采集共 9,997,824 点，中间 1.003 s 间隙明确保留，
不插值；两段跑流和休眠末 10 s 均未跨越采样间隙。首轮 App 已完成跑流，
但 BLU 端口会话失效使采集提前退出，因而没有有效功耗数据；关闭失效会话、
重新 Serve 并通过短采集后才重跑，未在此重试中改电压、供电或固件。

传输后 5063 ms 自动进入 DSLP，设置 30 s 的低电流跨度约 43.729 s
（中间含上述采样间隙）；BOOT=0x2/AON=0x8，NAN 初始化 228 ms。
唤醒后保存配对重连/PONG 通过，App ready 2.152 s，无需重新配对。
前后两次 PASN type=2、没有 type=1；未见 fault、分配失败、栈溢出、
MACID 耗尽、cache 拒绝或 TCP abort。休眠电流仍缓慢下降，末段 10 s
不是完全稳定值。该轮完成后释放串口、保持 BLU 输出，并恢复了 COM5 发布。
随后用户要求恢复默认功率表，以同板对照功耗。

脱敏记录见 [blu-com15-power8.json](../../component/amebagreen2/nan/tests/results/20261118/blu-com15-power8.json)，
原始采样、UART、分析脚本和波形图在 `build/green2-blu-com15-20261118/`。


## 2026-11-18：COM15 恢复默认功率表的功耗对照

按用户要求移除固定 8 dBm 覆盖，`nan_transport.c` 恢复原始实现，保留 RF
只读诊断。完整构建的两核功能选项、深睡设置与上一轮一致；固件 900,768 字节，
SHA-256 `509a16eb8c4ddfecef45a00e898c16cefd9d95ee08a6d1227d7c520e27e3ca54`。
COM15 固件区烧录、逐字节读回及 NAN 启动通过，保留现有配对/校准分区。
后续仅清理不再使用的 include 和函数格式，严格编译通过；剔除 debug 信息后
目标文件与已烧固件中的对应目标文件完全一致。

BLU 仍为 3.3 V，启动读回 `force=0 force_qdbm=0 byrate=1 limit=1`，
TPC 配置/活动均为 0。ch6 查询速率均为 15 dBm；ch149 的 OFDM6/HT0/HE0
为 18 dBm、OFDM54 为 17 dBm、HT7/HE7 为 16 dBm、HE8 为 15 dBm、
HE9 为 14 dBm。这是驱动目标表读回，没有射频仪表校准实际发射功率。

默认表的约 5 s 发现态采集（100 kHz、499,712 点，剔除前 1 s）平均
51.911 mA / 171.305 mW；同方法 8 dBm 基线 51.483 mA，相差
+0.428 mA（+0.832%）。短窗口与 `Calibrated=0` 条件下未据此认定显著差异。

首轮准备时 iPhone 锁屏，未启动跑流采集。用户继续后确认已解锁，沿用
现有配对完成双向约 10 s 跑流、自动深睡、AON 唤醒和保存配对重连。

| 状态 | 8 dBm 平均电流 | 默认表平均电流 | 电流变化 | 接收端速率：8 dBm → 默认表 |
| --- | ---: | ---: | ---: | ---: |
| 手机 → Green2（上传） | 64.742 mA | 66.410 mA | +2.576% | 15.633 → 17.782 Mbps |
| Green2 → 手机（下载） | 124.686 mA | 134.692 mA | +8.025% | 21.335 → 23.850 Mbps |
| 自动深睡末段 10 s | 8.331 µA | 8.242 µA | — | — |

按 3.3 V 计算，默认表上传/下载平均功耗为 219.152/444.483 mW，
对应 8 dBm 的 213.649/411.462 mW。每端剔除 0.5 s 过渡区，默认表
统计窗为 9.054/9.007 s；两端字节数一致，分别为 22,347,776/29,818,880。
两段采集共 9,998,336 点，中间 1.210 s 间隙明确保留；跑流和休眠末 10 s
统计窗均未跨越间隙，未发现非法量程或残缺尾部。

默认表实测下载平均电流更高，但吞吐量也提高。本次仅每种配置各一轮，
没有固定 PHY 速率或进行重复轮次统计，不能将全部吞吐/电流差异归因于
发射功率。BLU `Calibrated=0`、电压未独立测量等限制与上节相同。

传输后 5029 ms 自动休眠，30 s 设置的低电流跨度约 43.951 s（含上述
采样间隙），已知 AON 定时偏差仍存在。BOOT=0x2/AON=0x8，NAN 初始化
230 ms；两次 PASN type=2，无新配对 type=1，唤醒后 PONG 成功，App
ready 4.482 s。两种功率配置各出现一次 `[NAN-W] dw early: nanctrl_propagation`，
对应轮次仍完成传输/深睡/重连；未见 fault、分配失败、栈溢出、MACID 耗尽、
cache 拒绝或 TCP abort。跑流及唤醒后保持 `force=0 limit=1`。

结束时 COM15 保留默认功率固件，NAN 活动/零 peer；BLU 3.3 V 输出保持，
采集串口已关闭，测试 App 已结束，COM5 发布已恢复。对照图在私有证据目录
`nan-power-comparison.png`，完整默认表波形在 `traffic/nan-traffic-power.png`。
脱敏记录见 [blu-com15-default-power.json](../../component/amebagreen2/nan/tests/results/20261118/blu-com15-default-power.json)，
私有证据和对照脚本位于 `build/green2-blu-com15-default-20261118/`。


## 2026-11-18：COM15 降至 0 dBm，并以空口确认档位变化

用户要求继续降功率并核查是否生效。将 NAN 启动前的目标改为 0 dBm，
明确设置 `b_tx_pwr_force_enbale=1`，因此零值不是关闭覆盖。每次 NAN 启动和
深睡唤醒均重新设置；其余构建功能选项与默认/8 dBm 轮次一致。新增只读
MAC 功率寄存器采样，完整双核构建与 transport/diagnostic 严格编译通过。
固件为 901,280 字节，SHA-256
`3ffe201311dc5f3ea86892d16548e05c3e466531e06b30d625e6c336d92d1766`。
COM15 固件区烧录及逐字节读回通过，保留配对、校准分区与 eFuse。

查验 hash-pinned NP 库：`wifi_set_tx_power` → `halrf_set_tx_power_by_user`
会填充逐速率表，并调用 `rtw_hal_mac_write_pwr_by_rate_reg`，写入
`0x4000d2c0..0x4000d2d8` 的 28 个半 dBm 条目。该路径受
`hal_data[0x2a9]` 写入保护字节控制；强制模式另关闭 MAC 功率限制位。
诊断在原有切信道快照中回读这些非破坏性寄存器，以及 mode/limit 状态，
不逐包打印或分配内存；扩大的批量打印缓存改为静态，避免增加 NP 任务栈。
启动、跑流和唤醒后的 ch6/ch36/ch149 均为 `force=1 force_qdbm=0`，
七个硬件功率字全部为零、`write_guard=0`，没有仅改变查询值而跳过写表。

另外使用 RouterOS hAP ax² 的空闲 wifi2 做相对空口测试。先确认该接口
无关联客户端，临时设为信道 6/20 MHz，限时抓包 12 s；每次完成后立即
恢复 2472 MHz、20/40 MHz。wifi1 维持原来信道 36 的 AP，未修改。
用 radiotap 数据只选相同发送地址、信道、6 Mbps NAN beacon，独立序号
计数与样本数一致。抓包顺序为默认表 → 0 dBm → 补测 8 dBm；补测后恢复
同 SHA-256 的 0 dBm 固件。没有为此删除配对或调整路由器的全局 sniffer。

| 发射设置 | 有效信标数 | 平均接收 RSSI | RSSI 范围 |
| --- | ---: | ---: | ---: |
| 默认表（ch6 查询目标 15 dBm） | 41 | -36.90 dBm | -39～-36 dBm |
| 8 dBm | 50 | -45.36 dBm | -48～-44 dBm |
| 0 dBm | 43 | -54.02 dBm | -56～-52 dBm |

8→0 dBm 的接收强度下降 **8.66 dB**，默认→0 下降 **17.12 dB**。
这支持 2.4 GHz 发现帧的实际发射随档位下降，并非仅日志变化；这是固定接收端
的相对 RSSI 对照，不是天线端绝对射频功率校准。5 GHz 数据路径本轮有信道
36 的硬件表回读和完整传输，没有另做 5 GHz 空口抓包。

| 状态 | 默认表 | 8 dBm | 0 dBm |
| --- | ---: | ---: | ---: |
| 手机→Green2 平均电流 | 66.410 mA | 64.742 mA | 64.896 mA |
| Green2→手机平均电流 | 134.692 mA | 124.686 mA | 124.857 mA |
| 上传吞吐 | 17.782 Mbps | 15.633 Mbps | 16.213 Mbps |
| 下载吞吐 | 23.850 Mbps | 21.335 Mbps | 21.534 Mbps |
| 自动深睡末段 10 s | 8.242 µA | 8.331 µA | 8.345 µA |

3.3 V 下，0 dBm 上传/下载平均功耗分别为 214.156/412.028 mW。
相对 8 dBm，平均电流仅 +0.237%/+0.137%，本轮没有额外整板节电证据。
按同一下载窗口，以 100 mA 阈值分类 SDK 换算后的样本：高电流样本均值
246.761→235.445 mA，占比 33.627%→35.930%，低电流均值均约 62.84 mA，
解释了平均值为何接近。这是供电电流幅度分类，不能当作直接测得的射频 TX
占空比，也未把变化全部归因于 PA；速率、重试和调度未被固定。

0 dBm 双向各约 10 s，两端字节一致（20,381,696/26,935,296），统计窗
9.057/9.010 s；100 kHz 两段共 9,998,336 点，中间 1.024 s 间隙明确保留。
传输后 5019 ms 自动深睡，30 s 设置的低电流跨度约 43.755 s（含间隙），
AON 定时偏差未修正。BOOT=0x2/AON=0x8，保存配对重连和 PONG 通过，
App ready 3.446 s；无 fault/分配失败/栈溢出/MACID 耗尽/cache 拒绝/TCP abort。
仍出现一次两组基线已有的 `[NAN-W] dw early: nanctrl_propagation`。

所有平均电流为 BLU 厂商 SDK 原样换算/滤波结果，保留负值；`Calibrated=0`，
3.3 V 未独立测量。每档各一轮跑流，未做固定 PHY 速率或多轮统计；新增寄存器
读取也属于 0 dBm 诊断固件的差异，不据此宣称严密的射频或能效校准结果。
脱敏记录见 [blu-com15-power0.json](../../component/amebagreen2/nan/tests/results/20261118/blu-com15-power0.json)，
原始 PCAP、UART、电流、分析脚本和 `nan-power-sweep.png` 在
`build/green2-blu-com15-power0-20261118/`。

最终恢复上述同 SHA-256 的 0 dBm 固件，逐字节回读、NAN 启动及硬件功率表
回读均通过。测试 App 已结束；BLU 仅关闭串口，保持 3.3 V 输出，关闭后
COM15 仍能响应 `nan status`。COM15/COM5 串口均释放，COM5 原有 NAN 发布
已恢复。RouterOS wifi2 为 2472 MHz、20/40 MHz、启用状态；wifi1 仍在信道
36，关联客户端数仍为 2。三个本轮远端 PCAP 已删除，本地副本和 SHA-256 保留。


## 2026-11-18：COM15 两核 tickless 与 AP 无效唤醒优化

本轮保持 0 dBm 目标，增加 `NAN_TICKLESS=ON`（默认开启，仅用于不带
NimBLE/TF-M/USB 的 standalone NAN）。最终固件 901,856 字节，SHA-256
`499e89522bf7bb1366d0e57baa4a88fd0554e42b67788f64bb3b3043f3be7b1d`；COM15 固件分区逐字节回读通过，保留配对、LittleFS、校准。
两核完整构建、受影响源文件严格编译及 16/16 ASan/UBSan 原生测试通过。
新增 PASN 定时测试覆盖无序截止时间、取消、到期、回调重装、向上取整和 tick 回绕。

两核原有 `configUSE_TICKLESS_IDLE` 和 `CONFIG_STANDARD_TICKLESS` 都是 1；
本轮修改运行时策略，让这个配置实际参与 CPU 空闲管理：

- AP 在 NAN/IPC 启动后释放启动锁，进入 retention CG，最长空闲 1000 ms，
  更早的任务截止时间或中断仍会唤醒。NAN 队列由 100 ms 轮询改为下个 PASN
  截止时间或 10 s 堆报告；串口改为 RX 中断驱动的阻塞等待，LOGUART 可唤醒 AP。
- NP 保留原有 `PMU_OS` 锁及无线固件的锁，CPU 空闲时停掉 SysTick，WFI 等待
  设备/IPC 中断或 PMC 截止时间，以 32 kHz 计数补偿 RTOS tick，最长 500 ms，
  保留 watchdog 的截止时间。提前唤醒后取消旧闹钟，后续设置闹钟会重新启用计数器。
- NP 的整域 CG 没有启用。中间候选放开 NP OS 锁后，`nan stop` 会触发整域 CG，
  虽仍输出 CG/唤醒日志，串口 RX 已不响应；保留 XTAL 也未解决。最终版恢复原有
  整域休眠限制，`nan stop`、停止后等待 12 s 再查询、`nan start` 均通过。
  失败证据保存在 `full-cg-candidate/`，不把该候选的电流记作最终结果。

最终启动日志：AP 第 20 s 累计 23 次 CG、20000 ms，NAN 队列仅 2 次超时；
NP 最近 10 s 累计 WFI 9838 ms，整域 sleep=0，
锁为 0x9（OS 加无线固件原有 bit 3）。因此“每 10 s 一次”指 NAN 队列超时，
不代表整个 AP 只有一次唤醒；射频/IPC 中断仍会唤醒 NP。

| 状态 | 修改前 0 dBm | 最终 tickless 0 dBm |
| --- | ---: | ---: |
| NAN 发现空闲平均电流 | 51.834 mA | 51.221 mA |
| 手机→模组上传平均电流 | 64.896 mA | 65.152 mA |
| 模组→手机下载平均电流 | 124.857 mA | 126.932 mA |
| 上传吞吐 | 16.213 Mbps | 16.267 Mbps |
| 下载吞吐 | 21.534 Mbps | 21.152 Mbps |
| 自动深睡末段 10 s | 8.345 µA | 8.182 µA |

发现电流变化 -0.613 mA（-1.18%）。只启用 AP CG 的
中间测量为 51.177 mA；因此本轮没有证明 NP 停 tick 带来额外可分辨的整板节电。
发现对照每组约 10 s、去掉首 1 s；跑流每方向约 10 s、去掉首尾各 0.5 s，
两端字节数一致（20,447,232/26,542,080）。均为 BLU SDK 原样换算，保留负值；
Calibrated=0，3.3 V 是输出设置，未独立测量。PHY/重试/射频环境未固定，每配置
只有一轮，不能由这些小差异推断统计显著性或固定吞吐增益。

最终版自动 DSLP、AON 唤醒、恢复保存配对及 PONG 均通过；App 重连 ready
3.967 s。30 s AON 设置对应低电流跨度 43.726 s，
原有 AON 计时偏差未修正。无 fault/分配失败/栈溢出/MACID 耗尽/cache 拒绝/TCP abort；
基线已有的 `dw early: nanctrl_propagation` 告警本轮 2 次。

脱敏记录见 [tickless-com15.json](../../component/amebagreen2/nan/tests/results/20261118/tickless-com15.json)。
原始 UART/电流、App 报告、波形、失败候选在 `build/green2-nan-tickless-20261118/`；
UART 可能含配对材料，不提交。结束时终止本轮 App，BLU 仅 Stop 关闭串口、
保持 3.3 V 输出，COM15 仍响应 NAN 状态；COM5 原有 NAN 发布已恢复，串口均释放。
RouterOS 未改动。代码未提交、未推送。

## 2026-11-18：COM15 两核 120 MHz 与运行态 CLDO 降一档

用户要求 CPU 降频并同步降核心电压。新增 `CPU_MHZ=120|160|192|240`，
默认保持 240；AP/NP 共用 USB PLL 的 CPU 分频，SRAM/ROM 随之降频，独立
外设分频及 PLL 频率保持原配置。两核启动刷新 `SystemCoreClock` 和 ROM 延时
校准，并用 `CPU_Clk_Check()` 校验硬件分频。此次实际验证 120 MHz，160/192
尚未做硬件测试。

标准运行电压 API 只有 0.9/1.0 V。本板读到 CLDO normal/live=7、sleep=3、
regulator state=6 后，实验使用固定 `CORE_LDO_CODE=6`：寄存器表标称
0.900→0.877 V，**没有实测核心引脚电压**。默认 `CORE_LDO_CODE=-1` 保留校准值；
低压选项只允许 120 MHz、standalone NAN tickless、无 BLE/TF-M/USB，并在
启动时核对时钟、运行态、参考电压及档位，只允许比已加载 normal 值低一档。
只写 CLDO normal/live 易失寄存器，sleep code、SWR、校准 Flash、eFuse 均保留。
SDK 在 `ameba_pmc_com.c` 中明确指出 0.8 V 会影响 RF，因此此实验不使用
0.8 V 休眠档冒充运行降压，也不能由 CPU 降频推断无线部分的电压裕量。

最终 `CPU_MHZ=120 CORE_LDO_CODE=6` 固件 902,816 字节，SHA-256
`c01d4007c39b49461fac4a5c65ad46f36bc0fc65f939d09dae12db798fd07e38`。
两核完整构建、COM15 固件逐字节回读、启动检查通过。新增时钟/CLDO/诊断代码
严格编译通过；AP 启动文件保留一个原有 `rtc_irq_init` 中 NULL→整数的编译告警，
单独允许该告警后检查，其余新代码无诊断。功率目标仍为 0 dBm，两核 tickless 开启。

| 状态 | 240 MHz / CLDO 7 | 120 MHz / CLDO 6 |
| --- | ---: | ---: |
| NAN 发现空闲 | 51.221 mA | 49.968 mA |
| 上传平均电流 | 65.152 mA | 59.244 mA |
| 下载平均电流 | 126.932 mA | 107.042 mA |
| 上传吞吐 | 16.267 Mbps | 9.224 Mbps |
| 下载吞吐 | 21.152 Mbps | 15.493 Mbps |
| 上传能量/数据量 | 13.217 mJ/Mbit | 21.196 mJ/Mbit |
| 下载能量/数据量 | 19.803 mJ/Mbit | 22.800 mJ/Mbit |
| 深睡末段 10 s | 8.182 µA | 8.025 µA |

单独降至 120 MHz、保留 CLDO 7 的中间固件发现电流为 50.591 mA，未跑新一轮
手机传输。最终降频降压组合的空闲电流相对 240 MHz 下降 2.45%，但跑流吞吐
下降更大：按 3.3 V 设置和实测平均电流/吞吐估算，上传/下载每 Mbit 耗能分别
增加 60.37%/15.13%。本轮结果不支持把该档作为改善传输能耗的配置。

双向各约 10 s，双方字节一致（11,599,872/19,398,656）；自动 DSLP、AON
唤醒和保存配对 PONG 通过，重连 ready 3.498 s。另加两次 3 s 设置的深睡唤醒，
均恢复 NAN；每次均打印 `CLDO active trim 6 -> 6`，没有逐次减档。启动、跑流、
唤醒后的 `[NAN-CLK]` 始终为 120000000 Hz/check=0/live=6/normal=6/sleep=3/state=6。
未发现 fault、内存分配失败、栈溢出或 MACID 耗尽。原有 AON 时间偏差仍存在。

上述是单板、单轮实验；PHY/射频环境未固定，BLU `Calibrated=0`，供电为 3.3 V
设置值，核心电压只有档位证据。没有温度/批次/长时间低压稳定性验证。结果见
[cpu120-cldo6-com15.json](../../component/amebagreen2/nan/tests/results/20261118/cpu120-cldo6-com15.json)，
原始记录及波形在 `build/green2-nan-cpu120-uv-20261118/`；单独降频记录在
`build/green2-nan-cpu120-20261118/`。恢复更高主频前应冷启动，让 ROM 重新加载
出厂调压参数；本次烧录脚本均使用 BLU 断电复位。

最终保留 120 MHz/CLDO 6 实验固件，BLU 3.3 V 输出保持开启、串口关闭，COM15
状态查询正常，COM5 原有 NAN 发布恢复。本轮 App 进程已终止，RouterOS 未改动。
代码未提交、未推送。


## 2026-11-18：COM15 恢复默认 CPU 频率与出厂电压

按用户要求结束降频降压实验，移除 `CPU_MHZ`、`CORE_LDO_CODE` 构建选项及
启动时的 CLDO 覆盖函数。时钟/电压启动文件恢复为基线 `257f321`：
`CLKCFG_0P9_RMII_USB`，USB PLL 960 MHz / 4，AP/NP 均为 **240 MHz**，
`CORE_VOL_0P9`。保留 tickless、PASN 到期唤醒优化，以及时钟/CLDO 只读诊断。
NAN 射频目标沿用 0 dBm。

重新核对 COM15 FTDI 板和 COM10 BLU，确认 NAN 活动且没有数据链路后，
使用 BLU 断电、设置 3.3 V、COM15 DTR→LOG_TX 进入下载模式。
固件烧录后再次断电启动，让 ROM 重新加载出厂调压；没有格式化 LittleFS、
改写校准 Flash 或 eFuse。完整双核构建、固件逐字节读回、16/16 ASan+UBSan
原生测试通过。固件 902,816 字节，SHA-256：
`4da22acdb2e804652476631648e87310fb2bb7a98a7c46dab07f971598406e36`。

冷启动与随后两次 `nan sleep 3` 的 DSLP/AON 唤醒均确认：

- AP/NP `CPU CLK: 240000000 Hz check=0`。
- CLDO `active=7 normal=7 sleep=3 state=6`，运行态恢复出厂档位。
- 每次唤醒后 NAN 启动成功，tickless 保持启用，无降压覆盖日志。
- 未见 CPU fault、栈溢出、内存分配失败或 MACID 耗尽。

0.9 V 是运行配置及档位标称值，未实测核心引脚电压。本轮仅做板端恢复验证，
没有重跑手机吞吐或功耗对照；上一节的 120 MHz/CLDO 6 结果属于历史实验。
3 s 设置仍有先前已记录的 AON 计时偏差。结束时仅 Stop 关闭 BLU 串口，
保留 3.3 V 输出；COM15 关闭 BLU 会话后仍响应 NAN 状态，NAN 活动/零 peer，
两侧串口均释放。COM5、手机、RouterOS 未改动。

脱敏记录见 [cpu-default-restore-com15.json](../../component/amebagreen2/nan/tests/results/20261118/cpu-default-restore-com15.json)，
私有构建、烧录、唤醒证据在 `build/green2-nan-default-cpu-20261118/`。
