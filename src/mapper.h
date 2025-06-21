#pragma once

#include <stdint.h>

typedef struct {
    uint16_t instruction_pointer;
    uint16_t registers_start;
    uint16_t registers_end;
} MapperDesription;

typedef struct {
    void *context;

    void (*map_memory)(void *context, uint8_t *memory);

    void (*register_write)(void *context, uint16_t pointer, uint8_t byte);

    MapperDesription (*description)(void *context);

    void (*free)(void *context);
} Mapper;

Mapper nrom_mapper(uint8_t *prg_rom, uint32_t prg_rom_size, uint8_t *chr_rom, uint32_t chr_rom_size);
