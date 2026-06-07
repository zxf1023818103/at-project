# RTL8721D (AmebaD) KM4 — layout-sensitive XIP boot hang — bug report for Realtek

## 1. Summary

On RTL8721D, KM4 (HP, Cortex-M33) image2 runs XIP from flash (virtual `0x0E000000`,
**RSIP encryption OFF / plaintext image**). We hit a **deterministic, position-sensitive
boot hang**: merely *linking* ~1 KB more code ahead of `app_start` (so the early boot
path shifts up by a few hundred bytes) makes KM4 take a **SecureFault inside `app_start`,
before `main()` / before `.bss` is zeroed**. The same code at the original layout boots
perfectly. The faulting code is never even reached at its intended target — the first
indirect branch in `app_start` jumps to a wild address.

We have ruled out (with on-target SWD evidence): code size, `app_start` alignment/pinning,
I/D-cache staleness, SPIC read width (4-byte vs 16-byte), FLASH MMU region bound, **absolute
flash address** (a uniform shift to the *same* `app_start` address boots), and **byte content**
(in-place `nop` of the trigger code, same size, still hangs). The one variable that decides
boot-vs-hang is the **boot path's position RELATIVE to the early/low XIP code**: any change to
that relative layout can trigger a KM4 XIP instruction-fetch fault (the first `blx` in
`app_start` fetches one 32-byte cache-line offset and branches to a wild address →
SecureFault before `main()`). This is an IP-layer XIP-fetch/prefetch behaviour; we need
Realtek's help to root-cause it and to get a layout rule that guarantees correct fetch.

## 2. Target / build context

- SoC: RTL8721D (AmebaD), KM4 = Cortex-M33 (HP), driver-only (no Ameba SDK app layer).
- image2 XIP: virtual base `0x0E000000`, **RSIP disabled** (plaintext flash image).
- Flash layout: `Image_All.bin` @ `0x08000000`; image2 @ flash `0x08006000`.
- Toolchain: arm-none-eabi-gcc 10.3.1 (ASDK), `-Os -mcpu=cortex-m33` (armv8-m.main+dsp, hard FP).
- Cache: SCB I/D cache (`rtl8721d_cache.h`: 32-byte line, "used for flash read and XIP").
  At `app_start` entry **cache is OFF** (CCR `0xE000ED14` = `0x00000201`, IC/DC = 0); the
  SDK enables it later, at `rtl8721dhp_app_start.c:416 Cache_Enable(ENABLE)`.

## 3. Symptom

KM0 boots normally; KM4 prints nothing (no `KM4 BOOT REASON`, no `main()` banner), no
HardFault dump, BLE never advertises. KM4 bootloader (image1, runs from RAM) validates the
image2 signature ("RTKWin") OK and jumps to `app_start` — so the hang is *inside* `app_start`
execution, before `main()`.

## 4. Minimal reproduction (deterministic toggle)

One function-call change in our NimBLE SM-crypto glue flips boot↔hang by changing how much
code is linked ahead of `app_start`:

```c
/* component/nimble/wolfcrypt_sm_glue.c, uECC_make_key() */

/* BOOTS — app_start @ 0x0e062c18 */
rc = mp_to_unsigned_bin_len(key->pubkey.x, p_public_key,      32);
rc = mp_to_unsigned_bin_len(key->pubkey.y, p_public_key + 32, 32);

/* HANGS — app_start @ 0x0e062fc4 (+0x3AC). wc_ecc_export_public_raw pulls in
 * wc_export_int -> sp_tohex/sp_radix_size (~1 KB) ahead of app_start. */
word32 xl = 32, yl = 32;
rc = wc_ecc_export_public_raw(key, p_public_key, &xl, p_public_key + 32, &yl);
```

The ~1 KB is **never executed at boot**; only its presence in XIP `.text` ahead of
`app_start` matters. Build: `make NIMBLE=ON`; flash `build/amebad/Image_All.bin` @ `0x08000000`.

**Key control experiment (proves it is not size):** padding 2 KB of dummy `.rodata` *after*
all `.text` (same final image size, `app_start` NOT shifted) → boots. So it is the *position*
of the boot path, not the image size.

## 5. Fault signature (KM4, via SWD; deterministic across runs)

| What | Value |
|------|-------|
| PC (spinning) | `0x10104468` (`BOOT_ROM_ResetVsr`) → `0x1010628e` (ROM fault trap), tight loop |
| LR | `0xFFFFFFB9` (EXC_RETURN: handler mode, MSP, non-secure) |
| ICSR `0xE000ED04` | `0x00400807` → VECTACTIVE = **7 (SecureFault)** |
| **SFSR `0xE000EDE4`** | **`0x00000048` = AUVIOL (bit3) + SFARVALID (bit6)** |
| **SFAR `0xE000EDE8`** | **`0x37FFE537`** (a wild address; execution branched here) |
| CFSR `0xE000ED28` / HFSR `0xE000ED2C` | 0 / 0 |
| `uxCurrentNumberOfTasks`, `ble_hs_sync_state`, … | uninitialized garbage → **`.bss` never zeroed** |

So: very early in `app_start`, control flow branches to `0x37FFE537`, the MPU/SAU
attribution unit rejects the fetch (AUVIOL), and the ROM fault handler spins.

**Localization:** with a HW breakpoint at `app_start` and breakpoints at its first callees
(ROM `0x10110794`, `VectorTableOverride`), neither callee is reached — the **first
`blx r3` in `app_start` already branches wild**, i.e. `app_start`'s own prologue is
fetched corrupt at this XIP address.

**Instruction-fetch corruption (root observation):** single-stepping `app_start` from its
entry, the PC step pattern (c4→c6→c8→**cc**→d0) matches the CPU executing the bytes the
linker placed at **`app_start+0x20`**, not at `app_start` — i.e. the XIP fetch for this
region is **offset by exactly one 32-byte cache line (+0x20)**. The debugger's AHB read of
the same XIP addresses shows the same +0x20 skew. So the first `ldr rN,[pc,#imm]` loads a
wrong literal and `blx` jumps to garbage.

## 6. What we ruled out (all on-target)

| Hypothesis | Test | Result |
|---|---|---|
| Image size / total XIP size | pad 2 KB `.rodata` after `.text` (same size, app_start not moved) | boots → it is **position**, not size |
| `app_start` position itself | linker `KEEP(*(.text.app_start))` pin to `0x0e000040` + restore the +1 KB | still hangs → the wild fetch is whatever code lands on the bad address, not `app_start` per se |
| I/D-cache stale lines | `Cache_Flush()` (SCB Invalidate I/D) in image1 right before the jump to `app_start` | still hangs → not stale cache (refill from flash still +0x20) |
| SPIC read width | cache is OFF at entry (4-byte reads); force `Cache_Enable()` in image1 (16-byte cache-line fills) | still hangs; **SFAR changes `0x37ffe537`→`0x37ff6537`** → both read modes mis-fetch, differently → not read-width |
| FLASH MMU region bound | read `RSIP->FLASH_MMU[1]` (`0x48000640`): `STR=0x0e000000 END=0x0e097fff OFF=0x05fe6000` | `app_start` (both layouts) is **inside** the mapped region, far from END → not an MMU overrun |
| Content vs address | build a *plain* (non-NimBLE) image with dummy filler so `app_start` lands on the **same** virtual `0x0e062fc4` (same MMU offset `0x05fe6000`) | **boots** → the hang is NOT a fixed bad virtual/physical address; it depends on the actual *code content/layout* in that XIP region |
| Absolute address (shift whole hang image) | `-Wl,--defsym=xip_text_gap=N` inserts N bytes at the head of XIP `.text`, shifting the **entire** hang layout (wolfCrypt code **and** `app_start`) by N. Swept N = 32, 64, 256, 1024, 4096, 8192 (app_start `0x0e062fe4`→`0x0e064fc4`, an 8 KB span) | **all 7 still HANG** → absolute flash address is irrelevant; with the hang content/layout intact the fault follows the layout, not the address |
| Uniform shift of *boot* content | same `--defsym` gap applied to the BOOT image (no wolfCrypt) | boots at every N → a uniform shift of good content never faults |

Taken together these triangulate the trigger:
- **Not absolute address**: shifting the whole hang image by 32 B … 8 KB still hangs (7/7).
- **Not size, not cache, not MMU region, not `app_start` alignment** (rows above).
- **Boot content + any shift → boots; hang content (wolfCrypt linked) + any shift → hangs.**

The sub-sections below (§6.1 map diff, §6.2 size-vs-content, §6.3 the decisive relative-offset
experiment) narrow this to a single statement: **the trigger is the boot path's position
RELATIVE to the early/low XIP code — not absolute address, not size in isolation, not byte
content.** Whatever changes that relative layout (linking `wc_export_int`/`sp_tohex`, or any
mid-`.text` insertion) can trigger it; a uniform shift that preserves the layout never does.

### 6.1 Symbol-map diff (boot vs hang)

`nm -n` diff of the two image2 ELFs:
- The +1 KB is not one block: `wc_ecc_export_public_raw` drags in 7 scattered wolfCrypt
  functions (`wc_ecc_export_ex`, `wc_export_int`, `sp_tohex`, `sp_radix_size`, `sp_div_d`,
  `_sp_div_small`), each inserted at its archive position, so the shift is **non-uniform**
  across XIP `.text` (clusters at +0x20, +0x3A8, +0x3AC, +0x3B0, +0x400 bytes in different
  regions). The whole low XIP region (`0x0e000020..0x0e044412`) shifts by exactly **+0x20**.
- `app_start` shifts **+0x3AC** (`0x0e062c18`→`0x0e062fc4`). In the BOOT image, address
  `0x0e062fc4` belongs to `log_service` (a runtime logging function, **not on the boot
  path**); in the HANG image it is `app_start`. Same address, same MMU map — but only when
  *boot-executed* code (`app_start`) occupies that region (with this surrounding layout)
  does it fault. The map shows no clean power-of-two / flash-block boundary that `app_start`
  crosses in hang-but-not-boot, reinforcing that the trigger is layout, not a single
  address line.

### 6.2 Minimal trigger — a ~164-byte SIZE change ahead of the boot path (content-independent)

Forcing individual archive symbols into the BOOT image via `-Wl,--undefined=<sym>` (no
source change; the function is **never called**, only linked, `--gc-sections` otherwise
drops it):

| Forced symbol(s) | sp_tohex code linked? | app_start | result |
|---|---|---|---|
| (none — boot image) | no | `0x0e062c18` | BOOT |
| `sp_tohex` (164 B) | **yes** | `0x0e062cd8` | **HANG** |
| `sp_radix_size` (+deps, larger) | no | `0x0e062db8` | BOOT |
| `sp_tohex,sp_radix_size` | **yes** | `0x0e062e80` | **HANG** |
| `sp_tohex` body stubbed to `return MP_OKAY` (symbol kept, code gone) | no | `0x0e062c18` | BOOT |

Forcing `sp_tohex` (164 B) into the boot image hangs; a *larger* sibling `sp_radix_size`
boots. So it is not the existence of "more code" — it is the specific cumulative size /
resulting layout. `sp_tohex` is on no boot path and is never executed.

**Content vs size — decisive in-place patch.** We then binary-patched `sp_tohex`'s 164 bytes
*in the flashed image* (it is never executed, so the bytes are free), keeping its size (so
`app_start` stays at `0x0e062cd8`):

| Patch of sp_tohex's 164 bytes | result |
|---|---|
| real code (unpatched) | HANG |
| first 82 B → `nop`, rest real | HANG |
| first 82 B real, last 82 B → `nop` | HANG |
| all 164 B → `nop` | HANG |
| (source) body stubbed to 4 B → **size shrinks**, `app_start`→`0x0e062c18` | BOOT |

All same-size variants HANG regardless of content; only shrinking it (removing the size/
shift) boots. **So the trigger is content-INDEPENDENT — it is the ~164-byte SIZE shifting
the boot path to a bad layout, not `sp_tohex`'s bytes.** `sp_tohex` was merely a convenient
164-byte vehicle; any same-size insertion at that point does it. This confirms §6's
relative-layout finding and rules out a specific byte/instruction pattern: the sensitivity
is to **code LAYOUT/size ahead of the boot path**, consistent with an XIP fetch/prefetch
issue keyed on instruction-fetch address/offset, not on flash content.

### 6.3 Decisive: it is the boot path's RELATIVE offset, not its absolute address

`-Wl,--defsym=xip_text_gap=N` inserts N bytes at the **head** of XIP `.text`, shifting the
WHOLE image uniformly (every function moves by N together — inter-function offsets
preserved). Sweeping N on the BOOT image:

| N (uniform head gap) | app_start | result |
|---|---|---|
| 0x40 | 0x0e062c58 | BOOT |
| **0xC0** | **0x0e062cd8** | **BOOT** |
| 0x400 | 0x0e063018 | BOOT |
| 0x1000 | 0x0e063c18 | BOOT |

Crucially, **`N=0xC0` lands `app_start` at `0x0e062cd8` — the *exact* address at which the
`sp_tohex` mid-insertion HANGS — yet it BOOTS.** The only difference: the head-gap shifts
the *whole* image (the early/low XIP code at `0x0e000000…` moves too, so app_start's offset
*relative to it* is unchanged), whereas `sp_tohex` inserts at `~0x0e04a000` and shifts only
the code *above* it (app_start moves **relative to** the fixed low XIP code).

**Conclusion (unifies every experiment): the trigger is the boot path's position RELATIVE to
the early/low XIP code — not its absolute address, not its size in isolation, not its byte
content.** A uniform shift preserves that relative layout → always boots (4/4 here, plus the
filler tests). A mid-`.text` insertion changes it → hangs (`sp_tohex`), unless the resulting
offset happens to be benign (`sp_radix_size`, +0x1A0 → boots). This is consistent with an
XIP instruction-fetch/prefetch mechanism keyed on the *relative addresses* of two code
regions (e.g. prefetch/cache-way aliasing between the early-boot code and `app_start`, or a
relative-reach constraint), and it is firmly a hardware-layer effect, not a software bug.

## 7. FLASH MMU dump (hang image)

```
RSIP_REG_BASE = 0x48000600 ; FLASH_MMU[8] @ 0x48000630 (CTRL/STRADDR/ENDADDR/OFFSET, 16B each)
FLASH_MMU[0]: CTRL=0x00000003 STR=0x0c000000 END=0x0c013fff OFF=0x03ffa000   (KM0 region)
FLASH_MMU[1]: CTRL=0x00000003 STR=0x0e000000 END=0x0e097fff OFF=0x05fe6000   (KM4 XIP image2)
FLASH_MMU[2..7]: 0
```
CTRL bit1 = OFFSET-minus. So KM4 XIP `0x0e000000..0x0e097fff` maps to flash with a
−`0x05fe6000` offset (≈ flash `0x0801a000`+). `app_start` at `0x0e062fc4` is well inside.

## 8. How to observe (SWD, this bench)

- Debugger: xPack OpenOCD 0.12 + RV CMSIS-DAP, `scripts/amebad_openocd.cfg` (KM4 = AP2
  Cortex-M33). KM0 = AP1.
- **`reset halt` lands in `BOOT_ROM_ResetVsr` (`0x10104468`) spinning on a reset handshake**
  (driver-only doesn't set it). To let boot proceed to `app_start` under the debugger,
  write `0x480003F8 = 0x02000201` once, then resume.
- `cortex_m vector_catch` does **not** catch the M33 SecureFault here; let it run free and
  read SFSR/SFAR after it hangs, or breakpoint `app_start` (HW BP) and single-step.
- OpenOCD single-step / register reads over RSIP/XIP are unreliable; trust HW-breakpoint
  hits, the post-hang SFSR/SFAR, and single-step *PC stride*.

## 9. Questions for Realtek

1. On a **plaintext (RSIP-off)** image2 with I/D cache **disabled** at `app_start` entry,
   why does the KM4 XIP instruction fetch return code offset by one 32-byte cache line
   (+0x20) once the boot path's **relative** position vs the early/low XIP code changes
   (while a uniform shift to the *same* absolute address fetches correctly)?
2. Is there a SPIC auto-read / XIP-cache / prefetch constraint that couples two code regions
   by their relative addresses (prefetch-buffer/cache-way aliasing, a relative-reach limit,
   a required alignment between the image2 entry/early code and the rest of `.text`)?
3. What layout rule guarantees correct XIP fetch across the whole image2 `.text` regardless
   of how much code precedes `app_start`?

Our current software workaround is to **avoid linking that ~1 KB of dead hex-export code in
the first place**: we export the ECDH public key with `mp_to_unsigned_bin_len()` instead of
`wc_ecc_export_public_raw()`, so `wc_export_int`/`sp_tohex`/`sp_radix_size` are never pulled in
and the boot-path layout is unperturbed. (We also verified an alternative — keep
`wc_ecc_export_public_raw` but gate off its unused `WC_TYPE_HEX_STR` branch so those symbols
are garbage-collected — which boots equally well, but it touches wolfSSL internals so we did
not adopt it.) Either way this only avoids *triggering* the fault; it does not address the
underlying XIP-fetch sensitivity, and any future change to the boot-path layout (more code
ahead of `app_start`) could re-trigger it. We would like a real fix or a documented layout
constraint.

---
*Generated from the at-project driver-only RTL8721D bring-up. See `doc/amebad_xip_layout_boot_hang.md` for the full investigation log.*
