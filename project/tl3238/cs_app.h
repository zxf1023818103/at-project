/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
void cs_app_init(void);
void cs_app_sync(void);
void cs_app_connect(uint16_t handle);
void cs_app_disconnect(uint16_t handle);
