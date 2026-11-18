# Bouffalo BL616 (E907) TrustZone / TF-M feasibility

Short answer: **BL616's T-head E907 RISC-V core has no ARM-style hardware TrustZone,
so the AmebaD TF-M secure-world approach is NOT portable to it.** This note records
why (incl. the Bouffalo vendor's own assessment), the contrast with the AmebaD TF-M
port, and what it means for this project so nobody re-evaluates it from scratch.

## Why AmebaD can do TF-M and BL616 cannot

- **AmebaD (RTL8721D) KM4 = ARM Cortex-M33** with the **ARMv8-M Security Extension
  (TrustZone-M)** — this is *hardware*: SAU/IDAU partition secure/non-secure address
  space, two banked CPU states (banked MSP/PSP + registers), `cmse` hardware call
  gateways, hardware secure↔non-secure state transitions. Our AmebaD TF-M port rides
  directly on this (SPE = secure world, `app_km4` = NSPE). See
  `doc/amebad_tfm_storage.md`, the `project_amebad_tfm_port` note.

- **BL616 / BL616CL = T-head E907 RISC-V** with only **M/U privilege modes + PMP**.
  There is NO in-core security-state extension equivalent to TrustZone-M. Bouffalo's
  own "TZ" is a **bus-level TZC (resource filter)**, not a core security state — a
  different mechanism from the ARM extension. The AmebaD SAU/IDAU/`cmse` machinery has
  no counterpart here, so the port does not carry over.

## Bouffalo vendor assessment (verbatim gist, 2026)

- E907 basically can't do it: only M and U modes, isolation leans on PMP, and the BSP
  already uses up almost all PMP entries by default.
- Their TZ is Bouffalo's own design, different from ARM's extension.
- Very hard; no material online. Internal assessment: **618DG** could do it, but the
  effort is huge — essentially the vendor building it from scratch, and the OS would
  have to switch to **Zephyr**. They've put some work into 618DG.
- Implementing a RISC-V TF-M is mainly work in the RISC-V **privileged mode + PMP**
  (implementation, switching, management) — that's the framework. The chip's TZC is
  mostly patching: carving key resources out to the SPE (TF-M).

## What a "RISC-V TF-M" actually requires

The only path on RISC-V (matching the vendor's description): an **M-mode monitor that
reconfigures PMP on every world switch** to swap the memory view, plus the bus TZC to
assign peripherals/RAM to the SPE. Existing ecosystem TEEs all need core/SoC support:
MultiZone (commercial, pure PMP + M-mode monitor — closest to "RISC-V TF-M"), Keystone
(academic, S-mode + PMP), and newer extensions Smepmp / CoVE (AP-TEE) / SiFive
WorldGuard. The E907 generation has essentially none of these.

**Hard constraint — PMP exhaustion:** E907 typically has only 8/16 PMP entries, and the
BSP already spends them on memory protection + XIP partitioning. Carving out a full SPE
isolation set (secure code/data/peripherals + NS view switching) needs more than are
left — a resource ceiling, not something software effort can dodge.

## Implications for this project

1. **No TF-M secure world / PSA encrypted secure storage (ITS+PS) on BL616.** That
   AmebaD capability (runtime-isolated keys the NS side can never read) is unavailable.
2. **HW crypto is unaffected — actually simpler.** Bouffalo's HACC/PKA acceleration
   (`MBEDTLS_*_ALT` in `component/bouffalo_sdk/mbedtls_port`) is **NS-direct**: the SEC
   engine is reachable from normal code, no TrustZone needed. Contrast AmebaD, whose
   IPSEC engine is a **secure-only** peripheral that *requires* TF-M's SAU to reach
   (see the `reference_amebad_hw_crypto` note). So "no TrustZone" is not a loss for
   crypto on BL616.
3. **Key protection falls back to eFuse + software.** On BL616 key confidentiality
   relies on the eFuse features — RSIP (OTF flash encryption) / RDP / ROTPK secure boot
   (`doc/bouffalo_keys.md`) — plus software measures. There is no hardware-isolated
   runtime key vault like AmebaD's TF-M ITS.

## Bottom line

AmebaD's TF-M works because a real ARMv8-M TrustZone-M backs it. BL616/E907 lacks that
hardware; the only equivalent is a PMP-monitor software TEE — large effort, capped by
PMP entry count. The vendor's read (from-scratch, Zephyr, only 618DG evaluated feasible)
is sound. Treat BL616 as: **no secure world; key protection via eFuse.**

## Cross-reference

- Bouffalo eFuse key/security inventory (RSIP/RDP/ROTPK + Sec_Eng/PKA/TZC): `doc/bouffalo_keys.md`
- Bouffalo boot-ROM API inventory: `doc/bouffalo_rom_symbols.md`
- AmebaD TF-M (the ARM-TrustZone-backed port this contrasts with): `doc/amebad_tfm_storage.md`
