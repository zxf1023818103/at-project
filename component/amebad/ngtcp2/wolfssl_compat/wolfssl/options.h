/*
 * Empty wolfssl/options.h shim.
 *
 * ngtcp2's crypto/wolfssl headers/sources #include <wolfssl/options.h>, which is
 * the artifact wolfSSL's autotools `configure` generates. This build configures
 * wolfSSL via user_settings.h (WOLFSSL_USER_SETTINGS, defined PUBLIC on the
 * amebad_wolfssl target) instead, so there is no generated options.h. Provide an
 * empty one to satisfy the include; settings.h then pulls user_settings.h.
 */
