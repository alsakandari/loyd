#include <stdio.h>

#include "emulator.h"

int main(int argc, const char *argv[]) {
    const char *program = argv[0];

    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom>\n", program);
        fprintf(stderr, "error: expected a file\n");

        return 1;
    }

    const char *rom_path = argv[1];

    Emulator emulator = {0};

    emulator_power_on(&emulator, rom_path);

    while (!cpu_status_is_break(&emulator.cpu)) {
        emulator_step(&emulator, 1024);
    }
}
