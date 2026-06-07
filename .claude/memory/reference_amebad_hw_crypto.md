---
name: reference_amebad_hw_crypto
description: RTL8720DN HW crypto (IPSEC engine) status in driver-only image — present but not callable; why; use ROM mbedtls instead
metadata: 
  node_type: memory
  type: reference
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

调查结论:**driver-only 最小 image 里 AmebaD 的 IPSEC 硬件加解密引擎"调不通"——但不是被阉割。**

事实链(实机 + 源码 `fwlib/crypto/rtl8721dhp_crypto_ram.c` 验证):
- **没被 fuse 掉**:efuse probe(`EFUSE_PMAP_READ8` 读 0x1C0/0x1C1 安全配置 + 0x190 的 16B RSIP key)全是 0xFF = 空白未 provision 的开发片;silicon block 在。
- **ROM API 在**:`CRYPTO_*` / `rtl_crypto_aes/des/3des/sha`(IPSEC 引擎)+ mbedtls 2.4.0(297 个函数)都在 ROM。symbol 地址在 `bootloader_km4/rlx8721d_rom_symbol_acut_boot.ld`,我抽成 `component/amebad/app_km4/rom_symbol_crypto.ld`(60 条 PROVIDE;CRYPTO_Init=0x10104a49、CRYPTO_Reset=0x10104829、rtl_crypto_aes_ecb_init=0x10103d19…)——该文件留在磁盘但**已从 build 撤线**。
- **init 成功、首次跑 AES 挂死**:`rtl_cryptoEngine_init()`=`CRYPTO_Init(NULL)` 返回 0;但随后 `rtl_crypto_aes_ecb_*` 第一次进 process 路径就 hang(实机:打印 "init=0" 后静默)。
- **机理**:`CRYPTO_Init`→`CRYPTO_Reset` 只对引擎基址做**纯写**(RST_ISR_CON/SWAP_BURST/DEBUG,DEBUG 含 ENGINE_CLK_EN|ARBITER_MODE),写被 buffer 吞掉所以"看起来成功";真正的**首次寄存器读**在 process 路径(`CRYPTO_ClearAllINT` 读 RST_ISR_CON、`CRYPTO_SetSrcDesc` 自旋读 SDSR)——读一个总线侧没起来的从设备→AHB 永不 ready→hang,软件 timeout 救不了。
- 所有 ROM crypto 函数都用 `TrustZone_IsSecure()` 选基址:secure→`CRYPTOS_REG_BASE=0x50022000`,non-secure→`CRYPTO_REG_BASE=0x40022000`(注意:不是文档泛指的 0x5007/0x4007)。init 与 process 选址逻辑一致,所以**不是 init/use 基址错配**。
- 缺的是**完整 SDK 的 secure boot 给 SEC 引擎做的总线级 bring-up**(安全属性/MPC + 时钟门控);最小 NTZ image 跳过了。试过 `RCC_PeriphClockCmd(APBPeriph_SECURITY_ENGINE, APBPeriph_SEC_ENG_CLOCK, ENABLE)` **不解决**(ENGINE_CLK_EN 是引擎内部时钟,不是总线门控/属性)。
- **2026-06 对比复测(更准的诊断)**:写了 HW-vs-SW 直测(`app_km4/rom_hwcrypto_test.c`,`#if 0` gated;同一 FIPS-197 AES-128-ECB 向量):SW(ROM mbedtls,use_hw=0)= **MATCH**;直接 `CRYPTO_Init(0)` 这次不是静默 hang 而是 **Secure HardFault**——`Hard Fault Patch (Secure)` / `Security Fault` / `Secure State:1`,**PC=0x10104a48(CRYPTO_Init 入口)**、R1=**0x48012000**(要碰的 crypto 外设基址)、CFSR/HFSR/BFAR 全 0(都是 NS 侧寄存器,故障在 S 侧)。即 **CRYPTO_Init 首次访问 crypto 外设(~0x48012000)就触发 SecureFault**:该外设在未配置的 SAU 下被 security-attributed 为 **secure-only**,NS 的 driver-only image 够不着。比"AHB hang"更本质——根因就是 secure 属性,不只是总线没起。两次现象不同(hang vs SecureFault)取决于走到哪个 alias/触发 SAU 还是 stall,结论一致:**HW 引擎在 NS image 不可达**。

**实用结论**:别折腾 IPSEC 引擎(补 secure 外设 bring-up 是闭源 silicon 的大坑、与本固件目标无关)。要加解密就用 **ROM 里的 mbedtls(纯软件,现在就能调)**。RSIP(片上 XIP flash AES-CTR 解密外设)同理依赖 efuse OTF key,本片空白→未启用。详见 [[reference_amebad_rsip_iv]]。

**ROM mbedtls 调用法(已落地 `component/amebad/mbedtls_rom/`,实机 KAT 8/8 PASS)**:A-cut ROM 是 **mbedtls 2.4.0**(指纹:无 `_ret` 后缀 + 有 `mbedtls_arc4_*` + 公有 `mbedtls_aes_encrypt`)。297 个符号已在 `app_km4/rlx8721d_rom_symbol_acut.ld`,链接自动解析,**不需要** rom_symbol_crypto.ld(那是 HW 引擎的)。

**最终形态 = 单一自包含头 `mbedtls_rom/include/rom_mbedtls_crypto.h`**:只声明 ROM 里真有的对称/哈希/MD-HMAC 原语(AES/ARC4/MD5/SHA-1/256/512),结构体按 ROM ABI 逐字复刻 + `_Static_assert` 锁 size(**aes=344、arc4=264、md5=88、sha1=92、sha256=108、sha512=216**;sha512 是 212B 成员被 8 字节对齐 pad 到 216)。**不拷 73 个 mbedtls 头、不要 mbedtls config、没有 `mbedtls/` 前缀**——从根上消掉下面那个 include 撞车。重的 PK/bignum/ECC/RSA/asn1/oid/ctr_drbg(~190 符号)不声明(结构体太纠缠),要用再单独抄。glue `rom_mbedtls.c` + `rom_mbedtls.h`(`rom_mbedtls_init()`),组件是 INTERFACE(SYSTEM)。

两个致命 ABI 坑(都已写进单一头/glue):
1. **aes ctx 必须 344**:ROM 带 `RTL_HW_CRYPTO` 编的,`mbedtls_aes_context` 多 `enc_key[32]+dec_key[32]`(344 而非标准 280)。app 侧若用 280 的结构体→ROM 的 `mbedtls_aes_init` memset(344) 越界 64B 踩内存。单一头直接把这两个字段写进结构体 + `_Static_assert(==344)`。
2. **`rom_ssl_ram_map.use_hw_crypto_func` 必须=0**:ROM aes/gcm/des 运行期读这个 gate 选软/硬件;=1 调进挂死的 IPSEC 引擎。`rom_mbedtls_init()` 设 0 + 接 calloc/free(经 `p_rom_ssl_ram_map->ssl_calloc`)。两个 struct view 叠同一块 ROM BSS(0x1000134c),use_hw_crypto_func 偏移不同,gate 走 `rom_ssl_ram_map`(`_rom_ssl_ram_map` 那个 view)写。

历史教训(为什么走单一头):**最初**拷了整棵 2.4.0 `mbedtls/` 头进来,结果 image link 路径上 `amebad_sdk_includes` 还把上游 3.6.4 的 `component/thirdparty/mbedtls/include` 挂成 `-isystem`,也有 `mbedtls/aes.h`;两个 SYSTEM include 3.6.4 先→`#include "mbedtls/aes.h"` 拿到 3.6.4 的(aes_ctx **280**)→ROM memset 344 进 280→**冲掉栈上 saved-LR,函数 return 时 pop pc=0→INVSTATE HardFault**(crypto 结果还对、8/8,只在 return 挂)。当时先用非-SYSTEM `-I` 顶住,**现在换单一头(无 `mbedtls/` 前缀)彻底无撞车**。教训:**单元编译验 sizeof 不够,得在真 image 的 include 上下文里验**。

其他两条:
- **md 层在 ROM 残废**:`mbedtls_md_info_from_type(SHA256)` 从 ROM 返回 NULL(虽然 `mbedtls_sha256_info` 符号在),md/HMAC 抽象层基本没法用→HMAC 直接用裸 `mbedtls_sha256` 手搓 ipad/opad。
- **fault 诊断**:`app_km4/cm_backtrace_stub.c` 原把 ROM HardFault handler 的 `cm_backtrace_fault` stub 成 no-op→只见 "Hard Fault Patch (Non-secure)" 没寄存器;改成 dump 异常帧 [R0..R3,R12,LR,PC,xPSR]+CFSR,PC=0/CFSR=0x20000(INVSTATE)直接定位 saved-LR 被冲。留着当调试件。
