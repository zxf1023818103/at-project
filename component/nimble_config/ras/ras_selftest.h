/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cs_distance.h"
/* Explicit synthetic input; never publishes a record or wireless result. */
int ras_synthetic_pair(struct ras_record *local, struct ras_rx *remote, float meters);
int ras_selftest(struct ras_record *local, struct ras_rx *remote,
                 struct cs_distance_work *work, struct cs_distance_result *result);
