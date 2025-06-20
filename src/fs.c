#include <errno.h>
#include <stdint.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "fs.h"
#include "exit.h"

uint8_t read_byte(FILE *file, const char *path) {
    uint8_t byte;

    int n = fread(&byte, 1, 1, file);

    if (ferror(file)) {
        fprintf(stderr, "error: could not read from file '%s': %s\n", path,
                strerror(errno));

        fclose(file);

        exit(1);
    }

    if (n != 1) {
        fprintf(stderr, "error: file '%s' is smaller than expected: was trying to read 1 byte\n", path);

        fclose(file);

        exit(1);
    }

    return byte;
}

uint8_t *read_bytes(FILE *file, const char *path, int amount) {
    uint8_t *bytes = malloc(amount);

    int n = fread(bytes, 1, amount, file);

    if (ferror(file)) {
        fprintf(stderr, "error: could not read from file '%s': %s\n", path,
                strerror(errno));

        fclose(file);

        exit(1);
    }

    if (n != amount) {
        fprintf(stderr, "error: file '%s' is smaller than expected: was trying to read %d bytes\n", path, amount);

        fclose(file);

        exit(1);
    }

    return bytes;
}

void read_bytes_into(uint8_t *bytes, FILE *file, const char *path, int amount) {
    int n = fread(bytes, 1, amount, file);

    if (ferror(file)) {
        fprintf(stderr, "error: could not read from file '%s': %s\n", path,
                strerror(errno));

        fclose(file);

        exit(1);
    }

    if (n != amount) {
        fprintf(stderr, "error: file '%s' is smaller than expected: was trying to read %d bytes\n", path, amount);

        fclose(file);

        exit(1);
    }
}
