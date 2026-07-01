# Bouffalo BL602 / BL616 / BL616CL ROM symbols

An inventory of the on-chip **boot-ROM API** each Bouffalo chip exposes, how the
build reaches it, and how it relates (or doesn't) to the crypto path. All figures
are measured from `component/bouffalo_sdk/<chip>_std/`.

## Mechanism: fixed ROM API table + index, not linker `PROVIDE`

None of the three chips expose ROM functions as linker symbols. Instead each ROM
holds a **table of function pointers at a fixed address**, and the SDK calls
through it (`RomDriver_Xxx` macro -> `ROM_APITABLE[idx]`). Consequences:

- ROM code costs **no flash** and does **not** appear as `PROVIDE(...)` in the map
  (the only `PROVIDE`s in `bl602_flash.ld` are stack/heap/wifi/board symbols).
- It is a runtime pointer dereference, so there is nothing to relocate or resolve
  at link time.

Two generations of the mechanism:

- **BL602** (SiFive E24, RV32): classic `RomDriver_*` macros indexing the table.
- **BL616 / BL616CL** (T-head **E907**, RV32): a `ROM_API_INDEX_*` enum used as the
  table index, plus a **`romapi_patch`** layer — C reimplementations that override
  ROM functions with known errata.

## Per-chip comparison

| | BL602 | BL616 | BL616CL |
|---|---|---|---|
| Core | SiFive E24 | T-head E907 | T-head E907 |
| ROM table base | `0x21010800` (`ROM_APITABLE`) | `0x90015800` (`ROM_APITABLE`) | `0x90019000` (`STD_ROMAPI_TABLE`) |
| Symbols | **169** `RomDriver_*` | **469** `ROM_API_INDEX_*` | **296** `ROM_API_INDEX_*` |
| RSVD placeholders | — | 3 | 3 |
| Patch layer | none | `bl616_romapi_patch.c` | `bl616cl_romapi_patch.c` |
| Files built into `<chip>_std` | `bl602_romapi.c` (+ `bl602_romdriver.c`) | `bl616_romapi_e907.c` + `_patch.c` | `bl616cl_romapi_e907.c` + `_patch.c` + `bl616cl_romdriver_e907.c` |
| Header | `bl602_romdriver.h` | `bl616_romdriver_e907.h` | `bl616cl_romdriver_e907.h` |

Index-section size on E907: `ROMAPI_INDEX_SECT_SIZE = 0x800` (`bl616_romdriver_e907.h`).

## Symbols by module

**BL602 (169):** SFlash 35 · GLB 19 · HBN 18 · AON 16 · PDS 14 · EF (eFuse) 9 ·
SF_Cfg 7 · plus Sec_Eng (AES/SHA/TRNG), clock helpers.

**BL616 (469 — the richest):** GLB 114 · HBN 77 · EF 51 · SF 50 · UART 36 ·
SFlash 36 · PDS 23 · AON 17 · MD 16 · L1C 15 · XIP 11 · CPU 11 · …

**BL616CL (296 — trimmed / more consolidated):** GLB 104 · HBN 97 · PDS 40 ·
AON 15 · bflb 14 · CPU 11 · Clock 5 · arch 2 · FUNC 2 · … (BL616CL's ROM drops the
separate UART/EF/SFlash index blocks BL616 carries and uses more grouped naming.)

The bulk on every chip is **GLB** (clock / reset / GPIO), **HBN** (low-power / RTC),
**PDS** (power domains), **AON** (always-on), **SFlash / SF** (serial flash) and
**EF** (eFuse) — i.e. early-boot bring-up and flash/power drivers kept in ROM to
save flash.

## Relationship to the crypto path

- **TRNG** — none of the three `<chip>_std` trees define `romapi_bflb_trng*`, so
  `bflb_sec_trng.c` runs its register implementation on all three; the entropy
  source does **not** go through ROM. (See `bflb_sec_trng.c` `BFLB_SEC_ENG_BASE`:
  BL602 = `0x40004000`, BL616/BL616CL = `0x20004000`.)
- **AES / SHA (Sec_Eng)** — BL602's ROM table has Sec_Eng entries, but the build
  uses the source drivers `bflb_sec_aes.c` / `bflb_sec_sha.c` (chip-agnostic; base
  address from the per-chip `config/<chip>/*_memorymap.h`), not ROM.
- **PKA (RSA / DH modexp, ECP)** — not in the ROM table at all; it is the separate
  precompiled per-chip library `bflb_lhal/src/pka/libpka_<chip>.a`
  (`libpka_bl602.a` / `libpka_bl616.a` / `libpka_bl616cl.a`).

So ROM is used mainly for **flash (SFlash/SF), clock/power (GLB/HBN/PDS/AON) and
eFuse (EF)**; the crypto primitives (TRNG / AES / SHA / PKA) do not depend on it.

## Cross-reference

- Crypto port / HW acceleration merge & activation: `component/bouffalo_sdk/mbedtls_port/`
  (see the `reference_bouffalo_mbedtls_port_merge` note).
- Chip key/eFuse inventory: `doc/bouffalo_keys.md`.
- AmebaD ROM comparison (ROM mbedTLS, RSIP/RDP/HUK): `doc/amebad_keys.md`,
  `doc/amebad_crypto_rom_mbedtls_vs_wolfcrypt.md`.
