#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mapper.h"

#define MEMORY_SIZE 0xFFFF

typedef struct {
    uint8_t memory[MEMORY_SIZE];
    uint16_t internal_clock;
    uint16_t instruction_pointer;
    uint8_t status;
    uint8_t stack_pointer;
    uint8_t accumulator;
    uint8_t register_x;
    uint8_t register_y;
    Mapper mapper;
} Cpu;

typedef enum {
    AM_IMPLICIT,
    AM_ACCUMULATOR,
    AM_RELATIVE,
    AM_IMMEDIATE,
    AM_ABSOLUTE,
    AM_ABSOLUTE_X,
    AM_ABSOLUTE_Y,
    AM_ZERO_PAGE,
    AM_ZERO_PAGE_X,
    AM_ZERO_PAGE_Y,
    AM_INDIRECT_JMP,
    AM_INDIRECT_X,
    AM_INDIRECT_Y,
} AddressingMode;

typedef enum {
    OP_PHP = 0x00,
    OP_PLP = 0x20,
    OP_PHA = 0x40,
    OP_PLA = 0x60,
    OP_JSR = 0x20,
    OP_BIT = 0x20,
    OP_BRK = 0x00,
    OP_ORA = 0x00,
    OP_AND = 0x20,
    OP_EOR = 0x40,
    OP_ADC = 0x60,
    OP_STA = 0x80,
    OP_LDA = 0xA0,
    OP_CMP = 0xC0,
    OP_SBC = 0xE0,
    OP_INC = 0xE0,
    OP_INX = 0xE0,
    OP_INY = 0xC0,
    OP_DEC = 0xC0,
    OP_DEX = 0xC0,
    OP_DEY = 0x80,
    OP_ISC = 0xE0,
    OP_BPL = 0x00,
    OP_BMI = 0x20,
    OP_BVC = 0x40,
    OP_BVS = 0x60,
    OP_BCC = 0x80,
    OP_BCS = 0xA0,
    OP_BNE = 0xC0,
    OP_BEQ = 0xE0,
    OP_RTS = 0x60,
    OP_ASL = 0x00,
    OP_SLO = 0x00,
    OP_LSR = 0x40,
    OP_JMP = 0x0C,
    OP_STX = 0x80,
    OP_STY = 0x80,
    OP_LDX = 0xA0,
    OP_LDY = 0xA0,
    OP_NOP = 0xea,
} OpCode;

void cpu_power_on(Cpu *);
void cpu_load_rom(Cpu *, const char *path);
void cpu_sync(Cpu *, uint16_t master_clock);
bool cpu_status_is_break(Cpu *);
