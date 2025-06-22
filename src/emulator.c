#include <stdint.h>

#include "cpu.h"
#include "emulator.h"

void emulator_power_on(Emulator *emulator) {
    emulator->master_clock = 0;

    cpu_power_on(&emulator->cpu);
}

void emulator_load_rom(Emulator *emulator, const char *rom_path) {
    cpu_load_rom(&emulator->cpu, rom_path);
}

bool emulator_stopped(Emulator *emulator) {
    return cpu_stopped(&emulator->cpu);
}

void emulator_step(Emulator *emulator, uint16_t steps) {
    emulator->master_clock += steps;

    cpu_sync(&emulator->cpu, emulator->master_clock);
}
