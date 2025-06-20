#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

int main(int argc, char *argv[]) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    Cmd cmd = {0};

    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cc_output(&cmd, "loyd");
    nob_cc_inputs(&cmd, "./src/main.c", "./src/cpu.c", "./src/emulator.c", "./src/fs.c", "./src/mapper.c");

    if (!cmd_run_sync_and_reset(&cmd)) {
        return 1;
    }

    if (argc > 1) {
        cmd_append(&cmd, "./loyd", argv[1]);

        if (!cmd_run_sync_and_reset(&cmd)) {
            return 1;
        }
    }
}
