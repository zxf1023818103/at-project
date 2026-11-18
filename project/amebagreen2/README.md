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

固件默认锁定底部 3 MiB，配置和安全存储仍可写。TF-M 通过受检查网关执行擦写，拒绝启动区、当前槽及由 NS 直接访问的安全存储请求。当前采用 TF-M 隔离级别 1；HUK 由 TRNG 生成并保存在安全 Flash，属于开发用存储后端，没有把普通 Flash 计数器当作硬件防回滚保证。

`dsleep rtc <秒>` / `dsleep uart rtc <秒>` 使用 AP 保留式 SleepCG，NP 保持无线、IPC 和看门狗任务运行；不进入整片断电。FreeRTOS 的下一次调度期限驱动 PMC 唤醒定时器，TF-M 启动时设置的安全权限在休眠前后保持不变。

`SBOOT`、`RSIP` 和 `EFUSE_DEBUG` 默认关闭。密钥文件默认为本地忽略的 `keys/com15.json`，可用 `SECURITY_KEYS` 覆盖；缺失时打包器拒绝自动替换设备密钥。Green2 密钥字段为 `root`、`app`、`rsip_data`、`rsip_tweak`、`rsip_app_data`、`rsip_app_tweak`（各 32 字节 hex），以及 `boot_iv`、`app_iv`（各 8 字节 hex）。启动 RSIP 使用 OTP 0x2c0/0x2e0，应用与 SPE 使用独立的 0x200/0x220 组。

`make flashloader` 用同一 root 密钥签名 RAM 下载器。主机签名和加密不会写 eFuse；只有已配置匹配熔丝的板卡才应烧录对应加密镜像。COM15 本次适配没有烧写 eFuse，也没有复用 COM42 的设备密钥。

来源见 [组件说明](../../component/amebagreen2/README.md) 和 [UPSTREAM.json](../../component/amebagreen2/UPSTREAM.json)，实测覆盖与限制见 [VALIDATION.md](VALIDATION.md)。
