/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "ras_codec.h"
struct cs_distance_work { float re[79], im[79]; };
struct cs_distance_result {
    int status;
    int32_t raw_mm, distance_mm;
    uint16_t counter, pairs, channels, span_mhz, residual_mrad;
};
/* Two-sided PBR phase slope. Valid range 0..60 m, >=15 channels spanning >=30 MHz.
 * Offset is an explicit calibration correction, never inferred from RSSI.
 * Work is caller-owned (632 bytes); no allocations or vendor host dependencies.
 */
int cs_distance_estimate(const struct ras_record *local, const uint8_t *remote,
                         unsigned len, int32_t offset_mm,
                         struct cs_distance_work *work, struct cs_distance_result *result);
