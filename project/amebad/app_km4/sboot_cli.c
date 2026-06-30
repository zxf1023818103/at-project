/*
 * sboot_cli.c — on-device, NON-DESTRUCTIVE secure-boot (Ed25519) self-test (make SBOOT=ON).
 *
 * AmebaD secure boot: at boot the KM4 ROM (boot_flash_hp.c BOOT_FLASH_Image2SignatureCheck) reads a
 * 32-byte Ed25519 public key from eFuse 0x1A0 and calls ed25519_verify_signature(sig, msg, len, pk)
 * over the image2 payload; on mismatch it dead-loops ("IMG2 SBOOT FAIL"). build_image_all.py
 * (SBOOT=ON) signs the KM4 image2 in exactly that format (SB_HEADER appended, sb_header field
 * patched), and sboot_keygen.py embeds the matching public key here as sboot_pk[].
 *
 * This command re-runs the SAME boot ROM verifier (rom_ed25519_verify_signature) over the SAME byte
 * range (0x0E000020 .. sb_header) on the running flashed image, with our test PK instead of the
 * eFuse PK. A PASS proves the whole chain (signing + format + byte range + ROM acceptance) — the
 * bootloader would accept this image once the eFuse PK is burned and the enable bit flipped. The
 * tamper case (flipped signature) returning FAIL proves rejection. No eFuse is touched.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "at_log.h"
#include "ameba_soc.h"        /* IMAGE_HEADER / SB_HEADER / IMAGE_HEADER_LEN, HAL_READ32, eFuse regs */
#include "sboot_pk.h"         /* static const unsigned char sboot_pk[32] - from sboot_keygen.py */

/* KM4 image2 XIP base: the IMAGE_HEADER lives here (image_addr = this + IMAGE_HEADER_LEN). Identical
 * to the bootloader's __flash_text_start__ - IMAGE_HEADER_LEN. */
#define SBOOT_IMG2_VMA   0x0E000000u

/* Boot ROM Ed25519 verify (rom_symbol ld: rom_ed25519_verify_signature). The exact function the
 * bootloader uses; returns 0 on success. */
extern int rom_ed25519_verify_signature(const unsigned char *sig, const unsigned char *m,
                                         unsigned long long mlen, const unsigned char *pk);

static BaseType_t cmd_sboot(char *out, size_t outlen, const char *cmd)
{
    const IMAGE_HEADER *hdr = (const IMAGE_HEADER *)SBOOT_IMG2_VMA;
    u32 sb_header = hdr->sb_header;
    u32 prot = HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_LP_EFUSE_PROTECTION);

    (void)cmd;
    log_info("[SBOOT] image2 @0x%08x: image_addr=0x%08x sb_header=0x%08x",
             (unsigned)SBOOT_IMG2_VMA, (unsigned)hdr->image_addr, (unsigned)sb_header);
    log_info("[SBOOT] eFuse protection=0x%08x  Secure-Boot(IMG2)=%s (this proof is non-destructive)",
             (unsigned)prot, (prot & BIT_SECURE_IMG2_DIS) ? "DISABLED" : "ENABLED");

    if (sb_header == 0xFFFFFFFF) {
        log_info("[SBOOT] image is UNSIGNED (sb_header blank) - the bootloader's SBOOT_FAIL path keys "
                 "on exactly this; build with SBOOT=ON to sign");
        snprintf(out, outlen, "sboot: UNSIGNED image -> see [SBOOT] log\r\n");
        return pdFALSE;
    }

    const SB_HEADER *sb = (const SB_HEADER *)sb_header;
    const unsigned char *msg = (const unsigned char *)(SBOOT_IMG2_VMA + IMAGE_HEADER_LEN);
    unsigned long long len = (unsigned long long)(sb_header - SBOOT_IMG2_VMA - IMAGE_HEADER_LEN);

    int rc = rom_ed25519_verify_signature(sb->sb_sig, msg, len, sboot_pk);
    log_info("[SBOOT] ROM ed25519 verify (msg %llu B): rc=%d -> %s",
             len, rc, (rc == 0) ? "PASS (signed image2 valid)" : "FAIL");

    /* tamper: flip one signature byte -> must be rejected (same crypto a tampered image would fail) */
    unsigned char bad_sig[64];
    memcpy(bad_sig, sb->sb_sig, sizeof(bad_sig));
    bad_sig[0] ^= 0x01;
    int rc2 = rom_ed25519_verify_signature(bad_sig, msg, len, sboot_pk);
    log_info("[SBOOT] tamper (flipped sig byte): rc=%d -> %s",
             rc2, (rc2 != 0) ? "FAIL = rejected (correct)" : "UNEXPECTED PASS (BUG)");

    snprintf(out, outlen, "sboot: verify=%s tamper=%s -> see [SBOOT] log\r\n",
             (rc == 0) ? "PASS" : "FAIL", (rc2 != 0) ? "rejected" : "BUG");
    return pdFALSE;
}

void sboot_cli_register(void)
{
    static const CLI_Command_Definition_t cmd_sboot_def = {
        "sboot",
        "sboot:\r\n verify the running signed image2 with the boot ROM's Ed25519 (non-destructive)\r\n",
        cmd_sboot, 0
    };
    FreeRTOS_CLIRegisterCommand(&cmd_sboot_def);
}
