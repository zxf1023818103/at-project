/*
 * AmebaD driver-only FreeRTOSConfig.h override (FreeRTOS-Kernel V10.6.2).
 *
 * The SDK ships a per-core FreeRTOSConfig.h under the project inc dir
 * (inc_lp / inc_hp), reachable on the include path via amebad_sdk_includes.
 * This file is found FIRST (the freertos_config INTERFACE puts this config/
 * dir ahead of inc_lp/inc_hp), pulls in that SDK base config via
 * #include_next, then applies the minimal deltas the driver-only V10.6.2
 * build needs. Same #include_next layering used by amebad/config/platform_opts.h.
 *
 * NOTE: do NOT guard with FREERTOS_CONFIG_H — that is the SDK config's own
 * guard; reusing it here would suppress the #include_next base config.
 */
#ifndef AT_AMEBAD_FREERTOSCONFIG_OVERRIDE_H
#define AT_AMEBAD_FREERTOSCONFIG_OVERRIDE_H

/* Base config: SDK inc_lp (KM0) / inc_hp (KM4) FreeRTOSConfig.h. It already
 * satisfies the ARMv8-M NTZ port prerequisites — configENABLE_MPU 0,
 * configENABLE_FPU 0(km0)/1(km4), configENABLE_TRUSTZONE 0, and
 * configRUN_FREERTOS_SECURE_ONLY 0 — and sets configUSE_16_BIT_TICKS 0
 * (V10.6.2 maps that to 32-bit ticks via its backward-compat path, so we must
 * NOT also define configTICK_TYPE_WIDTH_IN_BITS — that pair is a hard #error). */
#include_next "FreeRTOSConfig.h"

/* Minimal driver-only firmware: disable tickless idle. The SDK base config
 * turns it on and wires configPRE/POST_SLEEP_PROCESSING + portSUPPRESS_TICKS_
 * AND_SLEEP to external SDK PMU symbols (vPortSuppressTicksAndSleep,
 * freertos_pre/post_sleep_processing) that the minimal image does not link.
 * The upstream ARM_CM23/CM33 ports gate ALL tickless code on
 * configUSE_TICKLESS_IDLE == 1, so turning it off drops those external
 * references entirely. Re-enable + wire the PMU hooks when power saving is
 * brought up. */
#undef configUSE_TICKLESS_IDLE
#define configUSE_TICKLESS_IDLE 0

/* NB: configTICK_RATE_HZ must stay 1000 — bumping it (tried 2000) hard-faults
 * on boot (the SDK/driver hardcodes a 1 ms tick assumption somewhere).
 *
 * TCP throughput note (measured, see [[amebad-freertos-tcp-bringup]] memory):
 * TCP TX plateaus at ~11 Mbps. NOT the tick, NOT the window, NOT CPU clock
 * (200M is max), NOT the data copy (~1% of budget). The three pps numbers tell
 * the story:  UDP TX 1930 pps | UDP RX 1300 pps | TCP TX 930 pps.  RX clears the
 * 1 kHz tick (1300 > 1000), so it isn't tick-quantized; the single FreeRTOS+TCP
 * IP task is per-packet-PROCESSING bound, and TCP TX pays BOTH a send and (with
 * delayed-ACK) ~half an ACK-RX per segment serially: 1/(0.52 + ~0.5*0.77) ms ≈
 * ~1100 pps, matching ~930. That ~0.5-0.8 ms/packet (~100-150k cycles) lives in
 * the closed lib_wlan.a per-packet path (rltk_wlan_send/recv) + IP-task hop, so
 * ~11 Mbps is the practical TCP ceiling here; use UDP (22.7 Mbps) for bandwidth. */

#endif /* AT_AMEBAD_FREERTOSCONFIG_OVERRIDE_H */
