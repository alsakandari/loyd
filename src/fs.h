#pragma once

#include <stdint.h>
#include <stdio.h>

uint8_t read_byte(FILE *, const char *path);
uint8_t *read_bytes(FILE *, const char *path, int amount);
void read_bytes_into(uint8_t *, FILE *, const char *path, int amount);
