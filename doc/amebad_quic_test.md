# AmebaD (RTL8721D) QUIC client self-test — ngtcp2 + wolfSSL TLS 1.3

A QUIC v1 client on KM4 built on **ngtcp2** (transport) + the in-tree **wolfSSL**
expanded to a full **TLS 1.3 + QUIC** crypto layer, driven over the existing
**FreeRTOS-Plus-TCP** UDP socket bring-up. It performs a real QUIC handshake
against a host QUIC server and reports completion on the console.

> **Status (2026-06-08): integrates, builds, and flashes; on-hardware boot is
> blocked by the documented KM4 XIP/boot hang** — see §"Boot-hang block" below.
> The handshake has not yet run end-to-end on the board.

## Build

```
cd project/amebad && make QUIC=ON          # -> build/amebad/Image_All.bin (~892 KB)
```

`QUIC=ON` sets `-DAMEBAD_ENABLE_QUIC=ON`, which:
- forces `AMEBAD_WOLFSSL_TLS=ON` → wolfSSL builds the full TLS 1.3 + QUIC layer
  (`component/amebad/wolfssl/`: adds `ssl/internal/keys/tls/tls13/quic/wolfio/kdf`);
- builds ngtcp2 (`component/amebad/ngtcp2/`: core + `crypto/wolfssl`);
- compiles `app_km4/quic_test.c` and runs it from the network-up hook (instead of
  iperf); drops the iperf / ROM-mbedTLS / wolfCrypt-bench / monitor-shell TUs to
  shrink the image (there is no `--gc-sections` on this link).

The QUIC client target (server IP/port, ALPN) is at the top of
`component/amebad/app_km4/quic_test.c` (`QUIC_SERVER_*`, default
`192.168.4.250:4433`, ALPN `hq-interop`).

## Host QUIC server (WSL)

`project/amebad/quic_server/` holds an aioquic-based handshake-logging server +
a self-signed EC cert:

```
sudo apt install -y python3-aioquic        # this WSL has no pip/ensurepip
cd project/amebad/quic_server
python3 quic_handshake_server.py           # 0.0.0.0:4433, ALPN hq-interop
```

It logs every `HANDSHAKE COMPLETE alpn=...` / teardown. Ensure Windows Firewall
allows inbound UDP 4433 (the device → host packets; 192.168.4.250 is the mirrored
WSL/Windows IP). The device joins WiFi `AIoT`, so the server IP must be reachable
on that subnet.

## Flash + console

- Flash: `project/amebad/flash.ps1 -BootPort COM14 -FlashPort COM3 -Image .../Image_All.bin`
  (boot=COM14 drives CHIP_EN/strap; flash+log data=COM3).
- **Console: COM3 @ 1,500,000 baud** (the ROM banner is 115200, then the app
  switches to 1.5 Mbaud). Reset via a COM14 RTS pulse.
- On success you should see `[QUIC] ===> HANDSHAKE COMPLETE: QUIC v1 + TLS1.3
  over wolfSSL OK` on the device and `HANDSHAKE COMPLETE` on the server.

## Key integration decisions / gotchas

- **ngtcp2** (`component/amebad/ngtcp2/`): `NGTCP2_USE_GENERIC_SOCKADDR` (no
  `<sys/socket.h>`) + `NGTCP2_AF_INET/6`; `htonl/ntohl` provided in
  `ngtcp2_port.c`; malloc family = size-prefixed `pvPortMalloc` shim (no newlib
  `_sbrk`); empty `<wolfssl/options.h>` shim (config via user_settings, no
  `configure`); hand-written `generated/ngtcp2/version.h`.
- **wolfSSL TLS/QUIC** (`user_settings.h`, gate `AMEBAD_WOLFSSL_TLS`):
  `WOLFSSL_NO_SOCK` (else `wolfio.h` pulls sockets); stub `WOLFSSL_IP4/6` +
  `XINET_PTON` (x509 `MatchIpName` dead path under `VERIFY_NONE`); `NO_ASN_TIME`
  force-disables `HAVE_SESSION_TICKET`, so `quic_test.c` supplies a no-op
  `wolfSSL_CTX_UseSessionTicket`.
- **`sin_addr` macro collision**: FreeRTOS-Plus-TCP (`ipconfigIPv4_BACKWARD_COMPATIBLE=1`)
  `#define`s `sin_addr` → mangles `ngtcp2_sockaddr_in`. `quic_test.c` `#undef`s it
  before the ngtcp2 headers and uses `freertos_sockaddr.sin_address.ulIP_IPv4`.
- **newlib syscalls** (`quic_syscalls.c`): ngtcp2 `abort()` + TLS stdio pull
  `_write/_sbrk/_exit/...`; routed to LOGUART / a small static heap.
- wolfSSL XMALLOC/RNG glue split into `wolfcrypt_glue.c` (always linked when
  wolfSSL is) so the QUIC image can drop `wolfcrypt_bench.c`.

## Boot-hang block (the open issue)

The QUIC image flashes cleanly (ImageTool `SUCCESS`), but **KM4 image2 hangs
before `main()`** — the console stops at the KM0 `RRAM:` line with no `[KM4]`
banner. The symptom *looks* like the layout-sensitive XIP boot hang
(`amebad_xip_layout_boot_hang.md`), and it was first investigated as such — but
JTAG later proved the real cause is a **SecureFault / AUVIOL** (see "Real root
cause" below). The layout levers below were tried under the wrong assumption and
**all still hang** (identical `RRAM:`-then-silence signature) — which is exactly
what you'd expect, since the fault is a secure-attribution violation, not layout:

| Lever | Result |
|---|---|
| Trim diagnostics (→744 KB XIP) | HANG |
| Uniform XIP pad-sweep, 0/64/128/256/384/512/640/768/1K/2K/4K/8K/16K (`__xip_pad__` ld knob, verified `app_start` actually moves) | HANG (all) |
| Strip to WiFi+QUIC only (drop AT-cmd/monitor/shell) | HANG |
| Move fwlib boot path (`.text`+`.rodata`) to RAM execution (`app_start` → `0x1000fdc8` RAM, map-confirmed) | HANG |

The RAM-execution result was the first clue it's not a passive XIP stall (running
`app_start` from RAM didn't help) — and JTAG then confirmed the real cause.

### Real root cause (JTAG, 2026-06-08): SecureFault / AUVIOL — NOT the XIP layout hang

With the **RAM-move** build flashed (it keeps the KM4 core alive and halt-able;
the all-XIP build wedges the debug AP so it can't even be examined), halting the
hung KM4 over JTAG shows:

| Register | Value | Meaning |
|---|---|---|
| `pc` | `0x1010628e` | ROM; the halfword there is `0xe7fe` = `b .` (infinite spin) — a ROM fault handler |
| `lr` | `0xffffffb9` | EXC_RETURN → CPU is in **handler mode** |
| `xpsr` IPSR | `7` | exception 7 on Cortex-M33 = **SecureFault** |
| `CFSR` / `HFSR` | `0` / `0` | not a HardFault/Bus/UsageFault |
| **`SFSR`** | **`0x48`** | **AUVIOL (bit3, Attribution Unit Violation) + SFARVALID (bit6)** |

So the KM4 takes a **SecureFault during early boot**: the non-secure (driver-only)
QUIC image makes a **Secure-attributed access** (SAU/IDAU violation) that the
smaller baseline/NimBLE images don't. This is the same secure-attribution failure
mode as the HW crypto engine (see `reference_amebad_hw_crypto` — the crypto
peripheral ~`0x48012000` is secure-attributed and faults from this NS image).
**That is why pad / strip / RAM-move never helped — it was never a layout problem.**

> Side note: a JTAG `reset halt` (SYSRESETREQ core-reset) instead diverts to
> `pc=0x10104468` = `BOOT_ROM_ResetVsr` spinning on the `0x480003f8` bit25 reset
> handshake (the separate `reference_amebad_chplan_reboot_handshake` issue) — a
> reset-path artifact, not the natural-boot fault. A UART/CHIP_EN reset power-
> cycles the core and wipes debug vector-catch config, so catching the exact
> faulting instruction needs a setup that survives that.

### Pinpointed trigger (JTAG breakpoint bisect): `SOCPS_InitSYSIRQ_HP`

Bisecting `app_start`'s call chain with HW breakpoints (using SYSRESETREQ +
`mww 0x480003f8 0x02000201` to get past `BOOT_ROM_ResetVsr` and re-trigger):

- boot reaches `app_start` (`0x1000fdc8`) and `SystemSetCpuClk` returns fine, but
  the call `bl 0x10010430` never returns — the fault is inside it.
- `0x10010430` is a RAM-move long-call veneer (`ldr pc,[pc]`) forwarding to
  **`SOCPS_InitSYSIRQ_HP`** (`0x0e08bcac`) — the SOC power-save SYSTEM_ON IRQ init.
- Single-step trajectory: `0x0e08bcb0 → 0x0e08bcb2 →` (exception) `→` ROM
  `0x101109d0..e0 →` app `SecureFault_Handler 0x1007d15c` (= VTOR[7]) `→` spins at
  ROM `0x1010628e`. **The SecureFault fires right as `SOCPS_InitSYSIRQ_HP` runs.**

That function does `InterruptRegister(SOCPS_SYSIrq_HP, IRQ0/SYSTEM_ON, …)` then
tail-calls ROM `InterruptEn` (`0x101109d1`) to **enable the SYS IRQ**. So the NS
driver-only image, on the power-save SYS-IRQ bring-up, makes a Secure-attribution
access (AUVIOL) — same family as `reference_amebad_hw_crypto`.

**Open question — why only the big QUIC image?** The baseline (iperf) runs the same
`SOCPS_InitSYSIRQ_HP` and boots. Likely the larger image leaves the SYS IRQ
pending, or its VTOR (`0x1007c000`) IRQ0 slot isn't ready when SOCPS enables it, so
enabling vectors to a bad/secure address. Fix directions: ensure the SYS IRQ vector
is installed before SOCPS enables it / clear-pending the SYS IRQ first / configure
SAU to put the relevant power registers in NS.

### JTAG setup that works (for resuming)

- **xPack** `openocd.exe`: `C:\Users\zxf10\AppData\Roaming\xPacks\@xpack-dev-tools\openocd\0.12.0-7.1\.content\bin\openocd.exe`
  (bouffalo's bundled openocd is too old + its libusb can't open the probe).
- Probe **RV CMSIS-DAP** is a composite device `VID_D6E7 PID_3507` (MI_00 = DAP/WinUSB,
  MI_01 = COM7); pass `-c "cmsis_dap_vid_pid 0xd6e7 0x3507"`.
- `openocd -s <xpack>\.content\openocd\scripts -c "set ADAPTER_TYPE daplink" -c "set ENABLE_KM0 0" -f scripts/amebad_openocd.cfg -c "cmsis_dap_vid_pid 0xd6e7 0x3507" -c init -c halt -c "reg pc" ...`
  (KM4 gdb :3335). Resolve PC vs `build/km4/component/amebad/app_km4/target_pure_img2.axf`.
- To make the hung KM4 examinable, flash the **RAM-move** variant: temporarily add
  these two lines to `.ram_image2.text` in `component/amebad/app_km4/rlx8721d_img2_is.ld`
  (after `*(.image2.net.ram.text*)`), rebuild `make QUIC=ON`, flash:
  `*libamebad_ram_hp.a:*(.text*)` and `*libamebad_ram_common.a:*(.text*)`.
  The default (all-XIP) QUIC build wedges the debug AP (`Failed to read 0xe000ed00`)
  so the core can't be examined. Revert these lines afterwards (they touch the
  shared ld used by all KM4 images).

### To resume

1. Find the AUVIOL trigger: locate the Secure-attributed access in the boot path
   — check whether the larger image's sections cross into an IDAU/SAU Secure
   address range, or whether a newly-linked init touches a secure peripheral
   (e.g. the crypto engine ~`0x48012000`). Then configure SAU to place that
   region in NS, or keep the boot path off it.
2. Or escalate to Realtek with the SecureFault/AUVIOL evidence.
3. Functional validation independent of the board: build ngtcp2 + wolfSSL on the
   host and run the same client against `quic_handshake_server.py`.
