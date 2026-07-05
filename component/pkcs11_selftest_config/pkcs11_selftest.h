/*
 * pkcs11_selftest.h — cross-platform corePKCS11 token self-test.
 */
#ifndef PKCS11_SELFTEST_H
#define PKCS11_SELFTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run the corePKCS11 token self-test (keygen -> sign -> verify, persisted to LittleFS).
 * Prints [PK11] PASS/FAIL over at_log. Blocks for a second or two (EC P-256 keygen). */
void pkcs11_selftest_run( void );

/* Register the `pkcs11` FreeRTOS-CLI console command (runs pkcs11_selftest_run()). */
void pkcs11_cli_register( void );

#ifdef __cplusplus
}
#endif

#endif /* PKCS11_SELFTEST_H */
