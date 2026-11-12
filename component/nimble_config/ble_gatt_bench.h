#pragma once
#include "host/ble_gap.h"
int ble_gatt_bench_init(void);
void ble_gatt_bench_event(struct ble_gap_event *event);
