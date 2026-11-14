/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "nvs.h"
/* Diagnostics never expose stored values. Any disk failure quarantines writes
 * until restart; callers must not treat a RAM bond as a durable bond. */
int nvs_lfs_error(void);
unsigned nvs_lfs_commits(void);
void nvs_lfs_latch_error(int error);
