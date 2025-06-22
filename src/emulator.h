#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cpu.h"

typedef struct {
    Cpu cpu;
    uint16_t master_clock;
} Emulator;

void emulator_power_on(Emulator *);
void emulator_load_rom(Emulator *, const char *rom_path);
bool emulator_stopped(Emulator *);
void emulator_step(Emulator *, uint16_t steps);
