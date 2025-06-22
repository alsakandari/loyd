#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mapper.h"

#define RAM_SIZE 0xFFFF

typedef struct {
    uint8_t ram[RAM_SIZE];
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
    OP_PHP = 0x08,
    OP_PLP = 0x28,
    OP_PHA = 0x48,
    OP_PLA = 0x68,
    OP_JSR = 0x20,
    OP_BIT = 0x20,
    OP_ORA = 0x00,
    OP_AND = 0x20,
    OP_EOR = 0x40,
    OP_ADC = 0x60,
    OP_STA = 0x80,
    OP_LDA = 0xA0,
    OP_CMP = 0xC0,
    OP_SBC = 0xE0,
    OP_INC = 0xE0,
    OP_INX = 0xE8,
    OP_INY = 0xC8,
    OP_DEC = 0xC0,
    OP_DEX = 0xCA,
    OP_DEY = 0x88,
    OP_ISC = 0xE0,
    OP_BPL = 0x10,
    OP_BMI = 0x30,
    OP_BVC = 0x50,
    OP_BVS = 0x70,
    OP_BCC = 0x90,
    OP_BCS = 0xB0,
    OP_BNE = 0xD0,
    OP_BEQ = 0xF0,
    OP_RTS = 0x60,
    OP_ASL = 0x00,
    OP_ROL = 0x20,
    OP_ROR = 0x60,
    OP_LSR = 0x40,
    OP_STX = 0x80,
    OP_STY = 0x80,
    OP_LDX = 0xA0,
    OP_LDY = 0xA0,
    OP_SLO = 0x00,
    OP_JMP = 0x0C,
    OP_SEC = 0x38,
    OP_SED = 0xf8,
    OP_SEI = 0x78,
    OP_CLC = 0x18,
    OP_CLD = 0xd8,
    OP_CLI = 0x58,
    OP_CLV = 0xB8,
    OP_TAX = 0xaa,
    OP_TAY = 0xa8,
    OP_TSX = 0xba,
    OP_TXA = 0x8a,
    OP_TXS = 0x9a,
    OP_TYA = 0x98,
    OP_CPX = 0xe0,
    OP_CPY = 0xc0,
} OpCode;

void cpu_power_on(Cpu *);
void cpu_load_rom(Cpu *, const char *path);
void cpu_sync(Cpu *, uint16_t master_clock);
bool cpu_stopped(Cpu *);
