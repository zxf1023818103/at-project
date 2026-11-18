/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "osi/config.h"
#include <stdint.h>
config_t *ble_bond_config_new(const char *filename);
bool ble_bond_config_save(const config_t *config, const char *filename);
void ble_bond_config_defer(bool defer);
void ble_bond_config_capture_lenc(const uint8_t address[6], uint16_t ediv,
                                  const uint8_t rand[8]);
unsigned ble_bond_config_commits(void);
