#pragma once
#include <stdint.h>

void usb_bench_init(uintptr_t base);
int usb_bench_command(int argc, char **argv);
