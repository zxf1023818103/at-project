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
