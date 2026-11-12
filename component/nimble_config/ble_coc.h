#pragma once
#include "host/ble_gap.h"
int ble_coc_init(void);
void ble_coc_gap_event(struct ble_gap_event *event);
