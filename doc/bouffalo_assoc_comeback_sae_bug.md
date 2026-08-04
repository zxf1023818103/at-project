# WPA3-SAE: `ASSOC_REFUSED_TEMPORARILY` triggers a full re-authentication, breaking the 4-way handshake

Bug report for Bouffalo Lab. The defect is in the closed-source `fhost`/`macsw` firmware, so it
cannot be fixed or worked around from SDK source.

## Summary

When an AP answers an Association Request with status code 30
(`ASSOC_REFUSED_TEMPORARILY`) plus an Association Comeback Time — the normal 802.11w behaviour when
the AP still holds a previous association for that STA and is running SA Query — the driver waits
out its own response timer and then retries with a **full re-authentication** (`[FU]Auth retry`,
which re-runs the entire SAE exchange) instead of simply re-sending the Association Request after
the comeback time.

The association that follows does succeed, but the 4-way handshake then never completes: the AP
repeatedly sends EAPOL message 1/4 and never sends 3/4, ending in
`4-way handshake timeout, no EAPOL 3/4 received`.

This is a **normal power-cycle**: a STA that loses power without deauthenticating leaves a stale
association at the AP, and every subsequent boot hits this path.

## Environment

| | |
|---|---|
| Chip | BL618DG (B0 / Nuclei-900), dual-core AP+NP |
| SDK | `bouffalolab/bouffalo_sdk`, `fhost 1.7.10`, `macsw 1.7.10`, `boot2 8.2.4` |
| Toolchain | Nuclei GCC 14.2.1 (newlib) |
| Supplicant | SDK's vendored `wpa_supplicant`, built from source, driver-SME / external-auth mode |
| AP | MikroTik hAP ac² (RouterOS 7.22.3, wifiwave2), 5 GHz ch149, security = **`wpa3-psk`** (PMF mandatory) |
| STA MAC | `B4:E8:42:3C:B5:2A`   BSSID `DE:2C:6E:61:F4:C2` |

## Reproduction

1. Associate the STA to a WPA3-SAE (PMF) AP and let it reach the DHCP stage.
2. Hard-reset the STA (power cycle / reset pin) — i.e. **without** sending a Deauthentication frame.
3. Let it associate again.

Result: reproduces on roughly every second boot. The alternation is itself diagnostic — the failing
boot never associates, so it leaves no stale entry, and the boot after it succeeds.

Control: issue a Deauthentication before the reset (our `wifi forget`) and the reject, the
re-authentication and the failure **all disappear** — SAE runs once and the 4-way completes in
161 ms.

## Observed behaviour

Driver/supplicant log of a failing boot (timestamps are ms from reset release):

```
 1117 ms  [FU]Set timer in external auth start:2000000
 1164 ms  SAE-COMMITTED
 1305 ms  SAE-ACCEPTED                                    <- first SAE completes, 188 ms
 1305 ms  [FU]Clear timer in external auth end:status:0
 1321 ms  [FL]RECV ASSOCRSP,sn:1581
 1321 ms  Status ind SM_STATUS_IND_ID_ASSOC_REFUSHED_TEMP: 1001   <- status 30, comeback 1001 TU
 2344 ms  sm_rsp_timeout_ind_handler 284 , auth_retry:2    <- waits out the response timer instead
 2344 ms  [FU]Auth retry                                   <- *** re-authenticates ***
 2344 ms  [FU]Set timer in external auth start:2000000
 2391 ms  SAE-COMMITTED
 2533 ms  SAE-ACCEPTED                                     <- second, unnecessary SAE
 2549 ms  [FL]RECV ASSOCRSP,sn:1584
 2549 ms  sm_connect_ind                                   <- association succeeds
 ...      RX EAPOL 1/4 -> TX EAPOL 2/4, x4, one per second, 3/4 never arrives
 6679 ms  status detail: 4-way handshake timeout, no EAPOL 3/4 received
```

The AP's own debug log (`/system/logging/add topics=wireless,debug`) for the same event:

```
reauthenticating                          (x4)
associated, but was associated already    <- the AP confirms it still held the old association
reconnecting
disassociated, connection lost
```

Neither of the last three lines appears on the deauthenticate-first control run.

## Expected behaviour

Per 802.11 (and as every other STA implementation does), a STA that receives status code 30 with an
Association Comeback Time is still in State 2 (authenticated). It should wait out the comeback time
and re-send **only** the Association Request. Re-running SAE is neither required nor correct: the
AP has not asked for a new authentication, and it still holds a valid PMKSA for this STA.

## What we ruled out (please do not re-investigate these)

- **Not a PMK mismatch.** With supplicant debug enabled, the PMKID derived by the second SAE
  (`49 c6 be 87 …`) is byte-identical to the PMKID the AP echoes in EAPOL 1/4, on every retransmit.
  Both sides use the same PMK.
  ```
  SAE: PMKID                     - 49 c6 be 87 25 fc 98 a0 b5 37 76 31 9f ed 9f 9b
  RSN: PMKID from Authenticator  - 49 c6 be 87 25 fc 98 a0 b5 37 76 31 9f ed 9f 9b
  ```
- **Not the second SAE run per se.** We patched `sme_external_auth_trigger()` to reuse the completed
  SAE (report success without re-running it) on the re-trigger. Only one SAE runs, there is only one
  PMK — and the 4-way still fails identically.
- **Not a supplicant-side PTK input error.** `WPA: PTK derivation - A1=b4:e8:42:3c:b5:2a
  A2=de:2c:6e:61:f4:c2` is correct, and `WPA: Sending EAPOL-Key 2/4` is reached every time.
- **Not the PMK cache.** Enabling `CONFIG_PMK_CACHE_IN_MGMR` (so `wpa_sm_set_pmk()` populates
  `wifi_mgmr_pmk_cache_entry_update()`) changes nothing — the driver still re-runs SAE.
- **Not recoverable from the application layer.** After the failure, retrying within the same boot
  never succeeds (observed to 138 s). Only a chip reset recovers, which suggests the driver itself
  is left in a state the connect path cannot clear. Note also that calling `wifi_sta_disconnect()`
  while not associated leaves the driver unresponsive to subsequent `wifi_sta_connect()` calls —
  every later attempt then times out after 12 s with no log output at all.

## Requested fix

On receiving an Association Response with status code 30 and an Association Comeback Time, wait for
the comeback interval and re-send the Association Request, without tearing down the authentication
state or re-running SAE.
