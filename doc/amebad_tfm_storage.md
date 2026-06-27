# AmebaD (RTL8721D) TF-M PSA 安全存储 — ITS + PS

在已工作的 TF-M secure world（见 `doc/`/memory 的 TF-M port）之上，给 KM4 加上 PSA
**Internal Trusted Storage (ITS)** 与 **Protected Storage (PS, 加密落盘)** 两个安全
存储服务。NS 侧（app_km4）通过 PSA client API 调用，数据落在 SPI flash，**跨重启持久**。

> **状态（2026-06-13）：✅ ITS + PS 全部实机验证通过。** `make STORAGE=ON`：
> ```
> [STG] ITS persistence PASS (survived reboot, size=32 len=32)
> [STG] PS persistence PASS (survived reboot, len=32)
> [STG] PS ciphertext-at-rest PASS (raw[0..3]=01 00 00 00)
> ```
> PS 的密文落盘已验证：明文 secret 头是 `A5 5A C3 3C`，裸读 PS flash `0x083F4000`
> 得到 `01 00 00 00`（FS 元数据/密文，非明文）→ NS 读不到明文。

---

## 1. 构建 / 烧录 / 验证

```bash
cd project/amebad
make STORAGE=ON            # 自动 implies TFM=ON；产出 build/amebad/firmware.bin
```

烧录（见 skill `amebad-imagetool-flash`，bench：boot=COM14 / data=COM3 @1.5M）：

```bash
PSPATH='\\wsl.localhost\Ubuntu\home\zxf10\at-project\project\amebad\flash.ps1'
IMG='\\wsl.localhost\Ubuntu\home\zxf10\at-project\build\amebad\firmware.bin'
powershell.exe -File "$PSPATH" -BootPort COM14 -FlashPort COM3 -Image "$IMG" -Baudrate 1500000
```

控制台（COM3 @ 1.5 Mbaud）期望：

```
[KM4] PSA: ioctl PING ... PASS              # 原有 Platform 分区
[KM4] eFuse HUK(0x150,32B): ret=0 blank=1 fingerprint=0x...   # §9 eFuse HUK 探针
[STG] PSA secure storage self-test
[STG] ITS persistence PASS (survived reboot, ...)
[STG] PS  persistence PASS (survived reboot, ...)
[STG] PS  ciphertext-at-rest PASS (...)
```

测试逻辑（`project/amebad/app_km4/tfm_storage_selftest.c`，scheduler 启动前单线程调用）：
首个 boot UID 不存在 → `psa_its_set`/`psa_ps_set` 写入已知 secret 并读回校验；之后每次
boot → 读回比对（持久化）。PS 额外裸读 `0x083F4000` 验证不是明文（加密落盘）。

---

## 2. Flash layout（SPI NOR 4 MB，base `0x08000000`，raw offset）

```
raw offset   size      region                              S/NS   备注
──────────── ───────── ─────────────────────────────────── ────── ─────────────────────
0x000000     16 KB     km0_boot_all.bin                    NS     🔒 SR2 写保护
0x004000      8 KB     km4_boot_all.bin                    NS     🔒  (底部 3 MB
0x006000    ~624 KB    km0+km4 image2 (OTA1)               NS     🔒   只读)
            (ends 0x0a2000)
0x0a2000     ~57 KB    tfm_s.bin  (TF-M SPE / "IMG3")       SEC    🔒 动态槽，4 KB 对齐，
            (ends 0x0b01f0)                                          紧跟 image2 之后
   …空…
0x183000      —        OTA2 image2 (备份槽，dev 未用)        NS     🔒
─── 0x300000  flash 写保护边界（上 1 MB 可写）──────────────────────────────────────
0x300000     960 KB    LittleFS（240 blocks）               NS     ✏️ 可写
─── 0x3F0000  LFS 结束；以下 64 KB 是从 LFS 顶部切出的"平台安全存储"区 ───────────────
  ├ 0x3F0000  16 KB      TF-M ITS  (2 blocks × 8 KB)        NS*    ✏️
  ├ 0x3F4000  40 KB      TF-M PS   (加密, 2 blocks × 20 KB) NS*    ✏️  可用~19.5 KB
  └ 0x3FE000   8 KB      OTP / NV counters (4 KB + backup)  NS*    ✏️
0x400000     —          flash end   （64 KB 正好用满，无空尾）
```

要点：
- **`tfm_s`（SPE，含 mbedcrypto）放动态槽**：`build_image_all.py` 把它 4 KB 对齐摆在
  merged image2 之后（现 `0x0a2000`），bootloader 运行时算出同一偏移并编程 RSIP MMU
  entry 4。必须停在 OTA2（`0x183000`）之前 —— 现 ends `0x0b01f0`，余量充足。
- **存储区在上 1 MB（可写）**：底部 3 MB 被 SR2 `CMP+TB+BP` 写保护（见
  `src/BoardFlashLock.cpp` / `doc/flash_layout.md`），所以 ITS/PS 放 `0x3F0000` 才能被
  secure world 写。`*` = flash 字节物理上 **NS 可读**（SAU 把 flash 归 NS）——这正是
  **PS 要加密**的原因：ITS 是明文但靠 PSA API 隔离，PS 是密文落盘（机密性）。
- 这三个 `TFM_HAL_*_FLASH_AREA_ADDR` 是**裸 SPI 偏移**（`0x3F0000`，不是 `0x083F0000`）：
  TF-M flash FS 直接把它喂给 `Driver_FLASH0.ReadData/ProgramData/EraseSector`。

> **✅ 与 LittleFS 的共存（已解决）**：LFS 区已从 1 MB 缩到 **960 KB**（`0x300000`–`0x3F0000`，
> 240 blocks），顶部 64 KB（`0x3F0000`–`0x400000`）正式划给本存储区。改动在共享的
> `component/littlefs_config/lfs_config.h`（`LFS_CFG_REGION_SIZE = 0x0F0000`）+
> `component/lfs_image/CMakeLists.txt`（`IMAGE_SIZE = 983040`）—— 跨 6 平台统一缩（block_count
> 写在 LFS superblock 里，一份 lfs.bin 要到处挂就得几何一致；其余平台顶部 64 KB 留空）。
> 所以**生产 `Image_All.bin`（`lfs.bin` dd 到 `0x300000`）不再覆盖存储区**：960 KB 的 lfs.bin
> 止于 `0x3F0000`，ITS/PS/NV 区在合成镜像里是 `0xFF`（空白），首个 boot 自动 format。
> 详见 [flash_layout.md](flash_layout.md) / [lfs.md](lfs.md)。
> （注意：缩 LFS 是 disk-format 变更，首个新固件 boot 时 LFS 会重格式化 → 旧 KV 数据清空。）

flash 区块数坑（boot 卡死根因，见 §7）：ITS 16 KB、PS 20 KB，`SECTORS_PER_BLOCK=2`
→ 每个 FS 2 个 8 KB block（ITS flash FS 要求 `num_blocks==2 || >=4`）。

---

## 3. Memory layout（SRAM 512 KB `0x10000000`–`0x10080000` + XIP）

```
SRAM（按地址做 S/NS 归属；SAU 在 0x10060000 切）
0x10000000 ┬ 0x10002000  NS handoff block（MSP_NS/entry/VTOR，bootloader 写）
           │ 0x10005000  ROM 保留低区（ROMBSS/RSVD/MSP_NS）
0x10005000 ├─ NS_DATA  364 KB ── app_km4(image2) .data/.bss/heap、FreeRTOS、wolfSSL …
0x10060000 ├─ S_DATA   128 KB ── TF-M SPE：.data/.bss、mbedcrypto heap、crypto engine
           │                      buf(~8 KB)、ITS/PS 缓冲、secure MSP/PSP 栈
0x10080000 ┘                      （NS heap 被 __tz_sram_top__ 夹到 0x10060000 封顶）

XIP（flash execute-in-place，cached；RSIP/MMU 映射）
0x0E000000 ┬─ 4 MB 窗 ── NS image2(app_km4) cached-XIP   [SAU: NS]
0x0E3FFFFF ┘              （线性映射 flash 0x08000000）
0x0E400000 ┬─ 384 KB 窗 ─ TF-M SPE(tfm_s) cached-XIP     [SAU: Secure]
           │   0x0E400000  secure 向量表 (0x400)
           │   0x0E400400  NSC veneers (.gnu.sgstubs)     [SAU: NSC]
           │   0x0E400800+ secure .text/.rodata（384 KB 里只用 ~57 KB）
0x0E45FFFF ┘   （bootloader RSIP MMU entry 4 → tfm_s flash 槽 0x080a2000）

ROM            0x10100000–0x1010A000  secure ROM（FLASH_*、LOGUART_PutChar… SPE 调的
                                       secure 别名地址）
               0x1010A000–0x101D4000  NS ROM
外设           NS 别名 0x40000000 / Secure 别名 0x50000000
```

要点：
- **两个 XIP 窗**：NS image2 @ `0x0E000000`（4 MB 线性）；secure SPE @ `0x0E400000`
  （独立的 RSIP MMU entry 4，bootloader 在 secure-jump 前编程）。384 KB 是冗余，实际
  只用 ~57 KB，可缩回接近 64 KB。
- **SRAM 在 `0x10060000` 切**：以下全 NS（NS app），顶部 128 KB 是 secure（S_DATA）。
  SAU/IDAU 强制隔离；NS FreeRTOS heap 被 `__tz_sram_top__=0x10060000` 夹住不越界。
- Milestone B 把 **S_CODE 64 KB → 384 KB**（`region_defs.h` + bootloader
  `RSIP_MMU_Config(4,0x0E400000,0x0E45FFFF,…)` + `FLASH_S_PARTITION_SIZE`，三处 lockstep）；
  **S_DATA 仍 128 KB**（mbedcrypto engine buf + heap 装得下）。

---

## 4. ITS / PS 落盘格式

ITS 与 PS 是**两层**：ITS 是底层文件系统（`its_flash_fs`），PS 是在 ITS 之上再包一层
**HUK-AEAD 加密对象 + 防回滚表**。"要数据落盘机密性必须走 PS"正是因为 ITS 本身不加密。

```
PS:  asset ─AES-CCM 加密(HUK 派生 key)→ ps_object_t ─┐
                                                      ├─► its_flash_fs（A/B 双块 + scratch 掉电安全）► flash
ITS: asset ─（本项目明文，靠 TrustZone 隔离）──────────┘
```

### 4.1 ITS —— 底层 `its_flash_fs`（带掉电安全的双块日志 FS）

源码 `secure_fw/partitions/internal_trusted_storage/flash_fs/`。整个 ITS flash 区切两类块：

**① Metadata block（两份，A/B 互为 swap）** —— 块头 + 两张表：

```
its_metadata_block_header_t {
    uint32_t scratch_dblock;    // 数据段 scratch 块的物理 ID
    uint8_t  fs_version;        // 文件系统版本 = 0x02（当前；0x01 向后兼容）
    uint8_t  metadata_xor;      // 整张元数据(不含块头)的 XOR 校验
    uint8_t  active_swap_count; // 元数据块被 swap 的次数（★必须最后写 = 原子提交点）
}
├─ its_block_meta_t[]   每个物理块一条：{ phy_id, data_start, free_size }
└─ its_file_meta_t[]    每个文件一条（见下）
```

**② Data block(s)** —— 文件正文紧凑排布。

每个文件的元数据条目：

```
its_file_meta_t {
    uint32_t lblock;                 // 文件落在哪个逻辑数据块
    size_t   data_idx;               // 在该数据块内的偏移
    size_t   cur_size;               // 当前大小
    size_t   max_size;               // 最大大小
    uint32_t flags;                  // 创建标志（低16位用户flag / 高位写标志）
    uint8_t  id[ITS_FILE_ID_SIZE];   // 文件 ID（UID + client_id 编码而来）
    // 仅当 ITS_ENCRYPTION 时才有： uint8_t nonce[], uint8_t tag[]  ← 本项目无
}
```

**掉电安全**：写操作走 scratch block + A/B metadata swap —— 新元数据先写进 scratch
metablock，**最后**才写 `active_swap_count`（原子提交）；`metadata_xor` 校验完整性。
断电只会回到旧 active block，不会半写。这就是 `num_blocks` 必须 `==2`（无 GC，纯 A/B）
或 `>=4`（带数据块轮转）、**不能是 1 或 3** 的原因（`its_flash_fs.c` 直接拒，见 §7 坑 1）。

> **本项目 ITS**：`ITS_ENCRYPTION` 未开 → **ITS 文件明文落盘**（file_meta 无 nonce/tag），
> 机密性靠 TrustZone（NS 读不到 SPE）。几何 `SECTORS_PER_BLOCK=2` → 逻辑块 = 2×4 KB = 8 KB，
> ITS 16 KB → 正好 2 个 block（满足 `==2`）。mbedcrypto 的 NV-seed 熵种子也存在 ITS。

### 4.2 PS —— ITS 之上的加密对象层

源码 `secure_fw/partitions/protected_storage/`。PS **复用同一套 `its_flash_fs`**（实例指向
PS flash 区），但存进去的不是裸数据：每个 asset 打包成 `ps_object_t`、**AEAD 加密后**再当
底层 fs 的一个"文件"写下去：

```
ps_object_t {
    ps_obj_header_t header {
        union ps_crypto_t crypto {       // ← PS_ENCRYPTION 时的头（明文存，做 AAD/定位）
            uint8_t  tag[16];            // AEAD MAC
            psa_storage_uid_t uid;       // 对象 UID（key label 用）
            int32_t  client_id;          // 属主 client ID（key label 用）
            uint8_t  iv[12];             // AEAD nonce
        }
        ps_object_info_t { current_size, max_size, create_flags }
    }
    uint8_t data[];      // ← 密文（AEAD 加密后的对象正文）
    uint8_t tag_iv[];
}
```

**加密参数（本项目实测配置）**：
- 算法 = **AES-CCM**（`PS_CRYPTO_AEAD_ALG=PSA_ALG_CCM`，比 GCM 省 code），见 §6。
- Key = **16 B**，由 **HUK（eFuse 0x150）经 HKDF-SHA256 派生**，运行时算、不落盘（见 §9）。
- nonce/IV = **12 B**，Tag = **16 B**。
- **AAD = uid + client_id**（key label）—— 把对象身份绑进认证，防止把密文搬到别的 UID 冒用。
- **单对象上限 `PS_MAX_ASSET_SIZE = 4 KB`**（`config.cmake` 用 `-DPS_MAX_ASSET_SIZE=0x1000` 强开，
  默认 profile 是 2 KB）—— 足够装单个 AWS-IoT 凭据（设备证书 ~1.2 KB / RootCA ~1.2 KB /
  私钥含 RSA-4096 ~3.2 KB 各一对象）。flash block = 20 KB（`SPB=5`），远大于对象 ~4.2 KB。
- **容量**：PS 区 40 KB = 2×20 KB block（`num_blocks==2`，data 与 metadata 同块、另一块 scratch），
  可用 ~19.5 KB —— 一套 AWS 凭据（cert+key+CA ≈ 4.5 KB）+ 充足余量（可再放多套 / endpoint /
  thing name）。**用法：cert / 私钥 / RootCA 各存一个独立 PS 对象**（别拼成一个 >4 KB 的 bundle）。

另外 PS 维护一张 **object table**（`ps_object_table.c`）：记录 `UID → 内部 fid` 映射 +
每对象版本号，配合 NV counter（§6 `PLATFORM_DEFAULT_NV_COUNTERS`）做 **rollback 保护**
（防止把旧密文整块换回去回滚）。

### 4.3 两者定位

| | ITS | PS |
|---|---|---|
| 底层 | `its_flash_fs` | **同一套** `its_flash_fs`（另一实例） |
| 落盘内容 | 明文文件（本项目） | `ps_object_t`：AEAD 密文 + crypto 头 |
| 机密性来源 | TrustZone 隔离（NS 读不到 SPE） | **HUK-AEAD 加密**（dump flash 也读不出明文） |
| 防回滚 | 无 | object table 版本号 + NV counter |
| 适合放 | SPE 内可信数据：派生密钥、NV-seed | 要落盘机密性的数据：凭据、token、用户密钥 |

> 实测佐证（§1 控制台）：PS 明文 secret 头 `A5 5A C3 3C`，裸读 PS flash `0x083F4000`
> 得到 `01 00 00 00`（FS 元数据/密文，非明文）→ NS dump 读不到明文。

---

## 5. 架构与接线

```
NS (app_km4)                       │ Secure (tfm_s SPE)
  main.c → tfm_storage_selftest.c  │  ┌─ ITS partition  ─┐
  psa_its_*/psa_ps_*  ──SG veneer──┼─→│  PS  partition    │→ Driver_FLASH0
  (interface/src/tfm_{its,ps}_api) │  │  Crypto(mbedcrypto)│  (cmsis_drivers/
  wolfSSL（NS 自己的，不进 SPE）    │  └─ Platform partition┘   Driver_Flash.c)
                                   │      OTP/NV/provisioning  → SPI flash
```

四个 partition 全是 **SFN model**（`connection_based=false`）→ 保持 SFN backend +
isolation L1，**不用 IPC**。

开关链（`make STORAGE=ON` → `-DAMEBAD_TFM_STORAGE`）：
| 层 | 文件 | 作用 |
|---|---|---|
| Makefile | `project/amebad/Makefile` | `STORAGE?=OFF`，ON 时 `TFM:=ON`，传 `-DAMEBAD_TFM_STORAGE` |
| option | `component/amebad/CMakeLists.txt` | `option(AMEBAD_TFM_STORAGE …)` |
| SPE 配置 | `app_km4_secure/CMakeLists.txt` | ON 时给 TF-M configure append `-DAMEBAD_TFM_STORAGE=ON` |
| TF-M 旋钮 | `platform/rtl8721d/config.cmake` | `if(AMEBAD_TFM_STORAGE)` 块（见 §6） |
| flash 区 | `platform/rtl8721d/partition/flash_layout.h` | `TFM_HAL_{ITS,PS}_FLASH_AREA_*` + OTP/NV area |
| flash 驱动 | `platform/rtl8721d/cmsis_drivers/Driver_Flash.c` | 真实读/写/擦（见 §8） |
| NS 接口 | `project/amebad/app_km4/CMakeLists.txt` | link `tfm_{its,ps}_api.c` + 测试源 + 定义宏 |
| NS 测试 | `project/amebad/app_km4/tfm_storage_selftest.c` | psa_its/psa_ps 往返 + 持久化 + 密文校验 |

---

## 6. config.cmake 旋钮（`if(AMEBAD_TFM_STORAGE)` 块）

```cmake
TFM_PARTITION_INTERNAL_TRUSTED_STORAGE ON
TFM_PARTITION_CRYPTO                   ON     # PS 加密硬依赖；拉 mbedcrypto 进 SPE
TFM_PARTITION_PROTECTED_STORAGE        ON
PS_ENCRYPTION                          ON
PS_CRYPTO_AEAD_ALG                     PSA_ALG_CCM   # CCM 比 GCM 小
CRYPTO_NV_SEED                         ON     # 无 TRNG → NV-seed entropy（seed 存 ITS）
CRYPTO_HW_ACCELERATOR                  OFF
PLATFORM_DEFAULT_OTP                   ON     # otp_flash.c：entropy seed / ROTPK（HUK 改走 eFuse，见 §9）
PLATFORM_DEFAULT_NV_COUNTERS           ON     # PS rollback
PLATFORM_DEFAULT_CRYPTO_KEYS           OFF    # 不拉 dummy 模板；HUK 改走 eFuse（§9）。-D 定义仍保持 global
PLATFORM_DEFAULT_PROVISIONING / TFM_DUMMY_PROVISIONING ON   # 开机 provision dummy（非生产！）

# crypto config 三件套（必须配套，否则 build 报 missing CIPHER/asym algorithms）：
PROJECT_CONFIG_HEADER_FILE              config/profile/config_profile_medium.h
TFM_MBEDCRYPTO_CONFIG_PATH              lib/ext/mbedcrypto/.../tfm_mbedcrypto_config_profile_medium.h
TFM_MBEDCRYPTO_PSA_CRYPTO_CONFIG_PATH   lib/ext/mbedcrypto/.../crypto_config_profile_medium.h
add_definitions(-DCONFIG_TFM_DOORBELL_API=0)   # 见 §7
add_definitions(-DPS_MAX_ASSET_SIZE=0x1000)    # 单对象 2 KB→4 KB（装 AWS 证书/RSA-4096 私钥）；
                                               # PS_MAX_ASSET_SIZE 是纯 C 宏(profile 头 #ifndef)，
                                               # 不是 cache 变量，必须 -D 强开（同 DOORBELL）
```

> **PS 几何**（`flash_layout.h`）：PS 区 40 KB，`TFM_HAL_PS_SECTORS_PER_BLOCK=5` → 2×20 KB block
> （保持 HW 验证过的 `num_blocks==2` 分支），可用 ~19.5 KB。block 20 KB 必须 > `PS_MAX_OBJECT_SIZE`
> (~4.2 KB)。ITS 仍 16 KB(2×8 KB)、NV 8 KB，三者填满保留的 64 KB（`0x3F0000`–`0x400000`）见 §2。

**entropy 链**（无硬件 TRNG）：开机 → dummy provisioning 把初始 64 字节 seed 写进 OTP
→ `crypto_nv_seed.c` 读 ITS 里的 NV seed，没有则从 OTP（`PLAT_OTP_ID_ENTROPY_SEED`）
取初值写进 ITS → mbedcrypto 用 NV seed 做 DRBG。**ITS 是 crypto 的前置依赖**。

mbedcrypto 编进来后 `tfm_s.bin` 仅 **~57 KB**（MinSizeRel + 裁过的 config：CCM/AES/
SHA/KDF/HMAC，无 RSA/cipher/asym-encrypt）。

---

## 7. 踩坑记录

1. **boot 卡死（只到 KM0 "RRAM" 行，无 KM4 banner）= ITS 区块数算错**。
   `TFM_HAL_ITS_SECTORS_PER_BLOCK` 一开始写成 `area/sector` = 16 KB / 4 KB = **4** →
   整个区 = 1 个 FS block；但 ITS flash FS 要求 `num_blocks == 2 || >= 4`
   （`its_flash_fs.c` 拒 1 和 3）→ FS init 失败 → SPM panic → SPE 启动卡死。
   **修：`=2`**（16 KB /(2×4 KB)=2 个 8 KB block）。PS 同理。

2. **secure 世界读 flash 别用 `FLASH_RxData`（user-mode 读）**。它把 SPIC 切出 auto
   模式，实测把 SPE 自己的 XIP 取指搞挂（同样表现为 boot 卡在 RRAM）。
   **读用 memory-mapped**（`*(volatile u8*)(0x08000000+addr)`，走 SPE 本来就在跑的
   auto/XIP 路径，不重配 SPIC）；写/擦才调 ROM `FLASH_TxData256B`/`FLASH_Erase`。

3. **SFN backend 下 crypto config 冲突**。`config_profile_medium.h` 是 IPC 向的，用
   `#ifndef` 强开 `CONFIG_TFM_DOORBELL_API=1`，跟 SFN backend 撞（`config_spm.h` #error）。
   **修：`add_definitions(-DCONFIG_TFM_DOORBELL_API=0)`**（命令行 -D 赢 header 的 #ifndef）。

4. **crypto module 与 mbedcrypto config 必须对齐**。光设两个 mbedcrypto config 路径不
   设 `PROJECT_CONFIG_HEADER_FILE` → CRYPTO 各 module 默认全开但算法没全 → build 报
   `missing CIPHER/asym algorithms`。`config_profile_medium.h` 会把 CIPHER/ASYM_ENCRYPT
   module 关掉对齐。

5. **改 -D / config 后重建 SPE**：别 `rm -rf` 整个 `build/km4/.../tfm_spe`（会重 clone
   mbedcrypto）；删 `tfm_spe/CMakeCache.txt` + `tfm_spe-prefix/src/tfm_spe-stamp/
   tfm_spe-configure` 即可强制 reconfigure 又留住 `mbedcrypto-src`。
   （首次 wipe 还有个坑：ExternalProject 的 CONFIGURE 先 `cd` 进 BINARY_DIR，删了整个
   目录会 "can't cd"；`mkdir -p` 回来即可。）

---

## 8. Secure 世界 flash 驱动

`project/amebad/app_km4_secure/platform/rtl8721d/cmsis_drivers/Driver_Flash.c`：

- **读** `ReadData`：memory-mapped，从 `0x08000000+addr` 直接拷（不重配 SPIC，cache 一致）。
- **写** `ProgramData`：调 ROM `FLASH_TxData256B`（≤256 B / page，不跨 256 B 边界），
  `__disable_irq` 包住。
- **擦** `EraseSector`：调 ROM `FLASH_Erase(EraseSector=2, addr)`，同样 IRQ 包住。
- ROM 函数用 **secure 别名地址**（同 `Driver_USART.c` 调 `LOGUART_PutChar@0x10102af5`
  的套路；地址取 secure bootloader 的 rom ld `bootloader_km4/rlx8721d_rom_symbol_acut_boot.ld`）：
  `FLASH_Erase=0x1010170d`、`FLASH_TxData256B=0x1010151d`。ROM 函数自己切 user 模式 + 做完
  恢复 auto，SPE 取指照常。

**关键结论：secure world 从 XIP 写 flash 可行**——之前担心的 XIP-write hazard 不是问题
（ROM `FLASH_Erase`/`FLASH_TxData` 跑在 ROM、做完恢复 auto 模式；ITS 开机 auto-format
就在写 flash，实测正常）。DCache 一致性：KM4 是标准 M33 DCache（`SCB_InvalidateDCache_by_Addr`
可用），实测 memory-mapped 读写一致，暂未额外 invalidate。

---

## 9. eFuse-backed HUK（PS 加密的真信任根）

PS 的存储密钥是从 **HUK（Hardware Unique Key）** 经 HKDF 派生的。默认 HUK 来自
`TFM_DUMMY_PROVISIONING` 写进**外部 flash OTP**——而外部 flash 字节 **NS 可读**，等于没保护。
RTL8721D 有 **片内 TrustZone-secure eFuse**（只有 secure world 能读），正好当真信任根。

### eFuse 用户区（`AN0411` + SDK `rtl8721d_efuse.h`）

```
0x150~0x17F  TrustZone Secure eFuse (EFUSE_SECURE_START)
  ├ 0x150~0x16F  32B  用户可用 secure eFuse  ← 拿来当 256-bit HUK
  └ 0x170~0x17F  16B  RDP key（硬件用）
0x130/0x140         User0/User1（Software R/W，NS 也能读 → 不能放秘密）
```

### 读法（SPE 内，照 SDK img3 `secure_efuse.c` 的 `Secure_ReadEfuse`）

```c
ctrl = *(volatile u32 *)0x40000810;      /* SYSTEM_CTRL_BASE 0x40000000 + REG_HS_EFUSE_CTRL1_S 0x810 */
for (i=0;i<32;i++)                        /* EFUSERead8 secure ROM 别名 = 0x10100a31 */
    ((u32(*)(u32,u32,u8*,u8))0x10100a31u)(ctrl, 0x150+i, &huk[i], 7 /*L25EOUTVOLTAGE*/);
```

常量从 secure bootloader 的 rom ld + 反汇编拿（同 `Driver_Flash` 调 ROM 别名的套路）。helper 放
`platform/rtl8721d/efuse_huk.h`（inline），eFuse 探针 IOCTL 和 crypto_keys 共用。

### 接进 TF-M

- **fork `crypto_keys.c`** → `platform/rtl8721d/services/src/crypto_keys.c`：只把 `tfm_plat_get_huk`
  改成读 eFuse（其余 policy/descriptor 表 + attestation-gated IAK 照抄模板，attestation OFF 编译掉）。
- **CMake 坑**：`PLATFORM_DEFAULT_CRYPTO_KEYS` **VAR 设 OFF**（不拉模板 `crypto_keys.c`），但
  **`-D` 定义保持 global**（`add_definitions(-DPLATFORM_DEFAULT_CRYPTO_KEYS)`）——`tfm_builtin_key_loader.h`
  用这个 `#ifdef` 选 HUK slot enum，crypto 分区和 `platform_crypto_keys` 必须看到同一个 enum。
  port CMakeLists（在 TF-M 默认 crypto-keys 块**之前**跑）往 `platform_crypto_keys` `target_sources`
  我们的文件 + include `interface/include/crypto_keys` + port 根（`efuse_huk.h`）。`platform_crypto_keys`
  的 link libs（带 header 路径）是无条件加的，所以 VAR OFF 也能编。
- **验证 IOCTL**：Platform 分区加了 `AMEBAD_IOCTL_EFUSE_HUK_INFO`（0x8721E0）读 0x150/32B，只回
  **非机密**摘要 `{blank, fingerprint}`（FNV 折叠哈希，不泄密）给 NS 打印。

### 实机结果 & 状态

最初（空白片）：`[KM4] eFuse HUK(0x150,32B): ret=0 blank=1 fingerprint=0x5b517625`
- `ret=0`：SPE **能读 secure eFuse**（最不确定的硬件路径已验证）。
- `blank=1`：这片 eFuse **空白**，HUK 暂时 = 全 `0xFF`（固定值，还不是真秘密）。
- PS 仍 PASS，且 `PLATFORM_DEFAULT_CRYPTO_KEYS=OFF` 下成功链接 → **dummy 模板被排除，我们的
  eFuse `crypto_keys.c` 是唯一 HUK 提供者**（否则链接缺 `tfm_plat_builtin_key_get_*`）。
- 改 HUK 源后旧 dummy-HUK 加密的 PS 数据解不开 → 首个 eFuse-HUK boot 自动 reformat，之后照常 PASS。

> **✅ 已烧（2026-06-25，bench 板，见 `doc/amebad_keys.md` §五）**：`0x150` 已烧 32 字节随机 HUK
> （`577c9a71…`），现在 `blank=0`、HUK 设备唯一。**代码没改**——`crypto_keys.c` 照读 `0x150`。
> 实测正如上一条所料：首个 eFuse-HUK boot **PS 自动 reformat（first-boot），但 ITS 存活** → AWS 设备私钥
> 没丢、MQTT 自动重连，不 brick、不用重新 provision（ITS 非 HUK 加密、PS 才是）。烧录走 `efuse_burn`
> 控制台命令（`make EFUSE_DEBUG=ON`），细节见 keys 文档 §五。

---

## 10. 局限 / 后续

- **与 RSIP 的关系（重要,见 `doc/amebad_keys.md` §一)**:RSIP(AES-128 OTF)加密的是**镜像/代码**,
  **不加密运行时写的数据**。开 RSIP 时,ITS/PS/LittleFS 这些运行时写的区**必须设成 No-RSIP mask 区**
  (`rtl8721d_bootcfg.c`),否则写进去的明文被 RSIP 读路径"解密"成乱码。**所以 RSIP 不给这些数据落盘
  机密性**——要数据加密落盘,靠的就是本文的 **PS(HUK-AEAD)**,或 app 层自己加密后存 LittleFS;RSIP 替不了。
  RSIP 管"代码防克隆",PS/软件加密管"数据保密",两者正交。
- **flash 区与 LittleFS 共存**（§2）：✅ 已解决 —— LFS 缩到 960 KB（止于 `0x3F0000`），
  顶部 64 KB 划给本存储区，生产 `Image_All.bin` 不再覆盖。改在 `component/littlefs_config` +
  `component/lfs_image`（跨平台统一）。缩 LFS 是 disk-format 变更，首个新固件 boot 会重格式化 LFS。
- **eFuse HUK / entropy**：HUK 区（0x150）**已烧随机 HUK**（2026-06-25，§9 / keys §五）；entropy 仍走
  `CRYPTO_NV_SEED`（AmebaD 无暴露的 TRNG ROM 符号，初始 seed 由 dummy provisioning 给，非生产）。
- **S_CODE 384 KB 冗余**：实际 SPE 仅 ~58 KB，可缩窗口。
- 非-QUIC TFM 镜像偶发在 FreeRTOS-Plus-TCP RX 路径 Hard Fault（与 storage 无关，
  `[STG]` 在 scheduler 前已 PASS）；疑似既有的 TFM+全驱动镜像网络 flakiness。
- **串口**：别用 PnP Disable/Enable-PnpDevice 解锁被占的 COM 口——会把 CH340 重枚举成 ghost 还掉线；
  直接 kill 占口进程或等它释放。
