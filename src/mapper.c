#include <malloc.h>
#include <stdint.h>
#include <string.h>

#include "mapper.h"

typedef struct {
    uint8_t *prg_rom;
    uint32_t prg_rom_size;
    uint8_t *chr_rom;
    uint32_t chr_rom_size;
} NromMapper;

void nrom_mapper_map_ram(void *context, uint8_t *ram) {
    NromMapper *mapper = context;

    memcpy(ram + 0x8000, mapper->prg_rom, mapper->prg_rom_size);

    if (mapper->prg_rom_size == 16 * 1024) {
        memcpy(ram + 0xC000, mapper->prg_rom, mapper->prg_rom_size);
    }
}

MapperDesription nrom_mapper_description(void *context) {
    (void)context;

    return (MapperDesription){
        .instruction_pointer = 0x8000,
        .registers_start = 0,
        .registers_end = 0,
    };
}

void nrom_mapper_free(void *context) {
    NromMapper *mapper = context;

    free(mapper->prg_rom);
    free(mapper->chr_rom);
    free(mapper);
}

Mapper nrom_mapper(uint8_t *prg_rom, uint32_t prg_rom_size, uint8_t *chr_rom, uint32_t chr_rom_size) {
    NromMapper *mapper = malloc(sizeof(NromMapper));

    mapper->prg_rom = prg_rom;
    mapper->chr_rom = chr_rom;

    mapper->prg_rom_size = prg_rom_size;
    mapper->chr_rom_size = chr_rom_size;

    return (Mapper){
        .context = mapper,
        .map_ram = nrom_mapper_map_ram,
        .description = nrom_mapper_description,
        .register_write = NULL,
        .free = nrom_mapper_free,
    };
}
