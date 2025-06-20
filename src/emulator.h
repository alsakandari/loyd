#pragma once

#include <stdint.h>

#include "cpu.h"

typedef struct {
    Cpu cpu;
    uint16_t master_clock;
} Emulator;

void emulator_power_on(Emulator *, const char *rom_path);
void emulator_step(Emulator *, uint16_t steps);
