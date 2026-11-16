/* P-256 acceleration using the Green2 ROM PKE API, as in the SDK ecc_alt.c.
 * Validate inputs with mbedTLS and enable only after an on-device comparison
 * against its software implementation. No private scalars are logged. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mbedtls/ecp.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <string.h>

static int ready;
extern int green2_nan_random(void *out, size_t size);
static int random_bytes(void *context, unsigned char *out, size_t size)
{ (void)context; return green2_nan_random(out, size); }

static int multiply(mbedtls_ecp_group *group, mbedtls_ecp_point *result,
                    const mbedtls_mpi *scalar, const mbedtls_ecp_point *point)
{
    if (group->id != MBEDTLS_ECP_DP_SECP256R1 ||
        mbedtls_ecp_check_privkey(group, scalar) || mbedtls_ecp_check_pubkey(group, point))
        return MBEDTLS_ERR_ECP_BAD_INPUT_DATA;
    _Alignas(4) unsigned char k[32], x[32], y[32], rx[32] = {0}, ry[32] = {0};
    int rc = mbedtls_mpi_write_binary_le(scalar, k, sizeof(k));
    if (!rc) rc = mbedtls_mpi_write_binary_le(&point->MBEDTLS_PRIVATE(X), x, sizeof(x));
    if (!rc) rc = mbedtls_mpi_write_binary_le(&point->MBEDTLS_PRIVATE(Y), y, sizeof(y));
    pke_ecp_group hardware_group;
    pke_ecp_point input, output;
    if (!rc) rc = pke_ecp_group_init_in_rom(&hardware_group, PKE_ECP_CURVE_SECP256R1);
    if (!rc) {
        pke_ecp_point_init_base_point(&hardware_group, &input);
        pke_ecp_point_init(&output);
        input.X_p = x; input.X_size = sizeof(x);
        input.Y_p = y; input.Y_size = sizeof(y);
        output.X_p = rx; output.Y_p = ry;
        rc = pke_ecp_mul(&hardware_group, &output, k, sizeof(k), &input);
    }
    if (!rc) rc = mbedtls_mpi_read_binary_le(&result->MBEDTLS_PRIVATE(X), rx, sizeof(rx));
    if (!rc) rc = mbedtls_mpi_read_binary_le(&result->MBEDTLS_PRIVATE(Y), ry, sizeof(ry));
    if (!rc) rc = mbedtls_mpi_lset(&result->MBEDTLS_PRIVATE(Z), 1);
    if (!rc) rc = mbedtls_ecp_check_pubkey(group, result);
    mbedtls_platform_zeroize(k, sizeof(k));
    mbedtls_platform_zeroize(x, sizeof(x)); mbedtls_platform_zeroize(y, sizeof(y));
    mbedtls_platform_zeroize(rx, sizeof(rx)); mbedtls_platform_zeroize(ry, sizeof(ry));
    return rc;
}

int green2_nan_p256_mul(mbedtls_ecp_group *group, mbedtls_ecp_point *result,
                        const mbedtls_mpi *scalar, const mbedtls_ecp_point *point)
{
    if (!ready) return MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;
    return multiply(group, result, scalar, point);
}

void green2_nan_p256_init(void)
{
    if (ready) return;
    mbedtls_ecp_group group;
    mbedtls_ecp_point hardware, software, base;
    mbedtls_mpi scalar;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&hardware); mbedtls_ecp_point_init(&software); mbedtls_ecp_point_init(&base);
    mbedtls_mpi_init(&scalar);
    TickType_t began = xTaskGetTickCount();
    int rc = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (!rc) rc = mbedtls_ecp_copy(&base, &group.G);
    for (unsigned i = 0; !rc && i < 3; ++i) {
        if (!i) rc = mbedtls_mpi_lset(&scalar, 2);
        else rc = mbedtls_ecp_gen_privkey(&group, &scalar, random_bytes, NULL);
        if (!rc) rc = mbedtls_ecp_mul(&group, &software, &scalar, &base, random_bytes, NULL);
        if (!rc) rc = multiply(&group, &hardware, &scalar, &base);
        if (!rc) rc = mbedtls_ecp_point_cmp(&software, &hardware);
        if (!rc) rc = mbedtls_ecp_copy(&base, &software);
    }
    ready = rc == 0;
    printf("[NAN] P-256 PKE self-test %s rc=%d ms=%lu\n", ready ? "PASS" : "software fallback",
        rc, (unsigned long)((xTaskGetTickCount() - began) * portTICK_PERIOD_MS));
    mbedtls_ecp_point_free(&base); mbedtls_ecp_point_free(&software); mbedtls_ecp_point_free(&hardware);
    mbedtls_mpi_free(&scalar); mbedtls_ecp_group_free(&group);
}
