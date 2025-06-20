#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "exit.h"
#include "fs.h"
#include "mapper.h"

static void cpu_status_set_carry(Cpu *cpu) { cpu->status |= 1; }
static void cpu_status_clear_carry(Cpu *cpu) { cpu->status &= ~1; }
static void cpu_status_set_zero(Cpu *cpu) { cpu->status |= (1 << 1); }
static void cpu_status_clear_zero(Cpu *cpu) { cpu->status &= ~(1 << 1); }
// static void cpu_status_enable_interrupt(Cpu *cpu) { cpu->status &= ~(1 << 2);
// } static void cpu_status_disable_interrupt(Cpu *cpu) { cpu->status |= (1 <<
// 2); } static void cpu_status_set_decimal_mode(Cpu *cpu) { cpu->status |= (1
// << 3); } static void cpu_status_clear_decimal_mode(Cpu *cpu) { cpu->status |=
// (1 << 3); }
static void cpu_status_set_break(Cpu *cpu) { cpu->status |= (1 << 4); }
// static void cpu_status_clear_break(Cpu *cpu) { cpu->status |= (1 << 4); }
static void cpu_status_set_overflow(Cpu *cpu) { cpu->status |= (1 << 6); }
static void cpu_status_clear_overflow(Cpu *cpu) { cpu->status |= (1 << 6); }
static void cpu_status_set_negative(Cpu *cpu) { cpu->status |= (1 << 7); }
static void cpu_status_clear_negative(Cpu *cpu) { cpu->status |= (1 << 7); }

static bool cpu_status_is_carry(Cpu *cpu) { return cpu->status & 1; }

static bool cpu_status_is_zero(Cpu *cpu) {
    return (cpu->status & (1 << 1)) != 0;
}

static bool cpu_status_is_negative(Cpu *cpu) {
    return (cpu->status & (1 << 7)) != 0;
}

bool cpu_status_is_break(Cpu *cpu) {
    return (cpu->status & (1 << 4)) != 0;
}

static bool cpu_status_is_overflow(Cpu *cpu) {
    return (cpu->status & (1 << 6)) != 0;
}

static void cpu_status_update_zero_and_negative(Cpu *cpu, uint8_t i) {
    if (i == 0) {
        cpu_status_set_zero(cpu);
    } else {
        cpu_status_clear_zero(cpu);
    }

    if (i & (1 << 7)) {
        cpu_status_set_negative(cpu);
    } else {
        cpu_status_clear_negative(cpu);
    }
}

void cpu_power_on(Cpu *cpu) {
    cpu->stack_pointer = 0xFD;

    memset(cpu->memory, 0, MEMORY_SIZE);

    cpu->mapper.map_memory(cpu->mapper.context, cpu->memory);

    cpu->instruction_pointer =
        cpu->mapper.description(cpu->mapper.context).instruction_pointer;
}

void cpu_load_rom(Cpu *cpu, const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "error: could not open file '%s': %s\n", path,
                strerror(errno));

        exit(1);
    }

    uint8_t magic[4];
    read_bytes_into(magic, file, path, 4);

    uint8_t expected_magic[4] = {'N', 'E', 'S', 0x1a};

    if (memcmp(magic, expected_magic, 4) != 0) {
        fprintf(stderr, "error: invalid magic: expected '%d', got '%d'\n",
                *(uint32_t *)expected_magic, *(uint32_t *)magic);

        fclose(file);

        exit(1);
    }

    uint32_t prg_rom_size = read_byte(file, path);
    uint32_t chr_rom_size = read_byte(file, path);

    prg_rom_size *= 16 * 1024;
    chr_rom_size *= 8 * 1024;

    uint8_t flag6 = read_byte(file, path);
    uint8_t flag7 = read_byte(file, path);

    // PRG RAM size in 8 KiB
    read_byte(file, path);

    // flag8
    read_byte(file, path);

    // flag9
    read_byte(file, path);

    uint8_t reserved[5];
    read_bytes_into(reserved, file, path, 5);

    if (flag7 == 0x44) {
        flag7 = 0;
    }

    uint8_t mapper_id_lsb = (flag6 >> 4);
    uint8_t mapper_id_hsb = (flag7 >> 4);
    uint8_t mapper_id = (mapper_id_hsb << 4) | mapper_id_lsb;

    if (flag6 & (1 << 2)) {
        uint8_t trainer[512];
        read_bytes_into(trainer, file, path, 512);
    }

    uint8_t *prg_rom = read_bytes(file, path, prg_rom_size);
    uint8_t *chr_rom = read_bytes(file, path, chr_rom_size);

    switch (mapper_id) {
    case 0:
        cpu->mapper = nrom_mapper(prg_rom, prg_rom_size, chr_rom, chr_rom_size);
        break;

    default:
        printf("error: unsupported mapper: %d\n", mapper_id);
        exit(1);
    }
}

static inline void mirror_pointer(uint16_t *pointer) {
    if ((*pointer & 0xE000) == 0) {
        *pointer &= 0x7ff;
    } else if ((*pointer & 0xE000) == 0x2000) {
        *pointer &= 0x2007;
    }
}

static inline bool is_io_register(uint16_t pointer) {
    if ((pointer & 0xfff8) == 0x2000) {
        return true;
    } else if ((pointer & 0xffe0) == 0x4000) {
        return true;
    } else {
        return false;
    }
}

static inline void cpu_memory_write_byte(Cpu *cpu, uint16_t pointer,
                                         uint8_t byte) {
    mirror_pointer(&pointer);

    if (is_io_register(pointer)) {
        return;
    }

    cpu->memory[pointer] = byte;
}

static inline uint8_t cpu_memory_read_byte(Cpu *cpu, uint16_t pointer) {
    mirror_pointer(&pointer);

    if (is_io_register(pointer)) {
        return 0;
    }

    return cpu->memory[pointer];
}

// static inline void cpu_memory_write_word(Cpu *cpu, uint16_t address,
//                                          uint16_t word) {
//     uint8_t lsb = word & 0xFF;
//     uint8_t hsb = (word >> 8) & 0xFF;
//
//     cpu_memory_write_byte(cpu, address, lsb);
//     cpu_memory_write_byte(cpu, address, hsb);
// }

static inline uint16_t cpu_memory_read_word(Cpu *cpu, uint16_t pointer) {
    uint16_t lsb = cpu_memory_read_byte(cpu, pointer);
    uint16_t hsb = cpu_memory_read_byte(cpu, pointer + 1);

    return (hsb << 8) | lsb;
}

static inline uint8_t cpu_decode_byte(Cpu *cpu) {
    return cpu_memory_read_byte(cpu, cpu->instruction_pointer++);
}

static inline uint16_t cpu_decode_word(Cpu *cpu) {
    uint16_t word = cpu_memory_read_word(cpu, cpu->instruction_pointer);
    cpu->instruction_pointer += 2;
    return word;
}

static uint16_t cpu_decode_operand_pointer(Cpu *cpu,
                                           AddressingMode addressing_mode) {
    switch (addressing_mode) {
    case AM_ACCUMULATOR:
        return cpu->accumulator;
    case AM_RELATIVE:
    case AM_IMMEDIATE:
        return cpu->instruction_pointer++;
    case AM_ABSOLUTE:
        return cpu_decode_word(cpu);
    case AM_ABSOLUTE_X:
        return cpu_decode_word(cpu) + cpu->register_x;
    case AM_ABSOLUTE_Y:
        return cpu_decode_word(cpu) + cpu->register_y;
    case AM_ZERO_PAGE:
        return cpu_decode_byte(cpu);
    case AM_ZERO_PAGE_X:
        return cpu_decode_byte(cpu) + cpu->register_x;
    case AM_ZERO_PAGE_Y:
        return cpu_decode_byte(cpu) + cpu->register_y;
    case AM_INDIRECT_JMP: {
        uint16_t pointer = cpu_decode_word(cpu);

        if ((pointer & 0xff) == 0xff) {
            // Account for JMP hardware bug
            // http://wiki.nesdev.com/w/index.php/Errata
            return cpu_memory_read_byte(cpu, pointer) +
                   (((uint16_t)cpu_memory_read_byte(cpu, pointer & 0xff00))
                    << 8);
        } else {
            return cpu_memory_read_word(cpu, pointer);
        }
    }
    case AM_INDIRECT_X:
        return cpu_memory_read_word(cpu,
                                    cpu_decode_byte(cpu) + cpu->register_x);
    case AM_INDIRECT_Y:
        return cpu_memory_read_word(cpu, cpu_decode_byte(cpu)) +
               cpu->register_y;

    default:
        printf("error: unknown addressing mode: %d\n", addressing_mode);
        exit(1);

        break;
    }
}

static inline uint8_t cpu_decode_operand(Cpu *cpu,
                                         AddressingMode addressing_mode) {
    return cpu_memory_read_byte(
        cpu, cpu_decode_operand_pointer(cpu, addressing_mode));
}

static inline void cpu_push_byte(Cpu *cpu, uint8_t byte) {
    cpu_memory_write_byte(cpu, cpu->stack_pointer--, byte);
}

static inline uint8_t cpu_pull_byte(Cpu *cpu) {
    return cpu_memory_read_byte(cpu, ++cpu->stack_pointer);
}

static inline void cpu_push_word(Cpu *cpu, uint16_t word) {
    uint8_t lsb = word;
    uint8_t hsb = word >> 8;
    cpu_push_byte(cpu, lsb);
    cpu_push_byte(cpu, hsb);
}

static inline uint16_t cpu_pull_word(Cpu *cpu) {
    uint8_t hsb = cpu_pull_byte(cpu);
    uint8_t lsb = cpu_pull_byte(cpu);
    return (hsb << 8) | lsb;
}

// Add with carry implementation, separated from execution code, to be used with
// subtraction too
static void adc(Cpu *cpu, uint8_t rhs) {
    uint8_t lhs = cpu->accumulator;

    rhs += lhs;

    bool carry = rhs < lhs;

    uint8_t old_lhs = lhs;

    lhs = rhs;

    rhs += cpu_status_is_carry(cpu);

    carry = carry || rhs < lhs;

    // Check for carry bit
    if (carry) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    // Check for sign overflow
    if ((((old_lhs & (1 << 7)) == (lhs & (1 << 7))) &&
         ((old_lhs & (1 << 7)) != (rhs & (1 << 7))))) {
        cpu_status_set_overflow(cpu);
    } else {
        cpu_status_clear_overflow(cpu);
    }

    cpu->accumulator = rhs;

    cpu_status_update_zero_and_negative(cpu, cpu->accumulator);
}

static void branch(Cpu *cpu, bool condition) {
    uint8_t operand = cpu_decode_operand(cpu, AM_RELATIVE);
    int8_t relative = *(int8_t *)&operand;

    if (condition) {
        cpu->instruction_pointer += relative;
    }
}

// Branch if minus
static void cpu_execute_bmi(Cpu *cpu) {
    branch(cpu, cpu_status_is_negative(cpu));
}

// Branch if positive
static void cpu_execute_bpl(Cpu *cpu) {
    branch(cpu, !cpu_status_is_negative(cpu));
}

// Branch if carry set
static void cpu_execute_bcs(Cpu *cpu) { branch(cpu, cpu_status_is_carry(cpu)); }

// Branch if carry clear
static void cpu_execute_bcc(Cpu *cpu) {
    branch(cpu, !cpu_status_is_carry(cpu));
}

// Branch if overflow set
static void cpu_execute_bvs(Cpu *cpu) {
    branch(cpu, cpu_status_is_overflow(cpu));
}

// Branch if overflow clear
static void cpu_execute_bvc(Cpu *cpu) {
    branch(cpu, !cpu_status_is_overflow(cpu));
}

// Branch if equal
static void cpu_execute_beq(Cpu *cpu) { branch(cpu, cpu_status_is_zero(cpu)); }

// Branch if not equal
static void cpu_execute_bne(Cpu *cpu) { branch(cpu, !cpu_status_is_zero(cpu)); }

// Push processor status
static void cpu_execute_php(Cpu *cpu) {
    cpu_push_byte(cpu, cpu->status | 0x30);
}

// Pull processor status
static void cpu_execute_plp(Cpu *cpu) {
    cpu->status = (cpu_pull_byte(cpu) & 0xef) | (cpu->status & 0x10) | 0x20;
}

// Push accumulator
static void cpu_execute_pha(Cpu *cpu) { cpu_push_byte(cpu, cpu->accumulator); }

// Pull accumulator
static void cpu_execute_pla(Cpu *cpu) { cpu->accumulator = cpu_pull_word(cpu); }

// Jump to subroutine
static void cpu_execute_jsr(Cpu *cpu) {
    cpu_push_word(cpu, cpu->instruction_pointer - 1);
    cpu->instruction_pointer = cpu_decode_word(cpu);
}

// Return from subroutine
static void cpu_execute_rts(Cpu *cpu) {
    cpu->instruction_pointer = cpu_pull_word(cpu) + 1;
}

// Add with carry
static void cpu_execute_adc(Cpu *cpu, AddressingMode addressing_mode) {
    adc(cpu, cpu_decode_operand(cpu, addressing_mode));
}

// Subtract with carry
static void cpu_execute_sbc(Cpu *cpu, AddressingMode addressing_mode) {
    adc(cpu, ~cpu_decode_operand(cpu, addressing_mode));
}

// Logical And
static void cpu_execute_and(Cpu *cpu, AddressingMode addressing_mode) {
    uint8_t operand = cpu_decode_operand(cpu, addressing_mode);
    cpu->accumulator &= operand;
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator);
}

// Logical Or
static void cpu_execute_ora(Cpu *cpu, AddressingMode addressing_mode) {
    uint8_t operand = cpu_decode_operand(cpu, addressing_mode);
    cpu->accumulator |= operand;
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator);
}

// Exclusive Or
static void cpu_execute_eor(Cpu *cpu, AddressingMode addressing_mode) {
    uint8_t operand = cpu_decode_operand(cpu, addressing_mode);
    cpu->accumulator ^= operand;
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator);
}

// Compare
static void cpu_execute_cmp(Cpu *cpu, AddressingMode addressing_mode) {
    uint8_t operand = cpu_decode_operand(cpu, addressing_mode);

    uint8_t diff = cpu->accumulator - operand;

    if (cpu->accumulator >= operand) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    cpu_status_update_zero_and_negative(cpu, diff);
}

// Increment
static void cpu_execute_inc(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_memory_read_byte(cpu, pointer) + 1;
    cpu_memory_write_byte(cpu, pointer, new_value);
    cpu_status_update_zero_and_negative(cpu, new_value);
}

// Increment X
static void cpu_execute_inx(Cpu *cpu) {
    cpu->register_x += 1;
    cpu_status_update_zero_and_negative(cpu, cpu->register_x);
}

// Increment Y
static void cpu_execute_iny(Cpu *cpu) {
    cpu->register_y += 1;
    cpu_status_update_zero_and_negative(cpu, cpu->register_y);
}

// Decrement
static void cpu_execute_dec(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_memory_read_byte(cpu, pointer) - 1;
    cpu_memory_write_byte(cpu, pointer, new_value);
    cpu_status_update_zero_and_negative(cpu, new_value);
}

// Decrement X
static void cpu_execute_dex(Cpu *cpu) {
    cpu->register_x -= 1;
    cpu_status_update_zero_and_negative(cpu, cpu->register_x);
}

// Decrement Y
static void cpu_execute_dey(Cpu *cpu) {
    cpu->register_y -= 1;
    cpu_status_update_zero_and_negative(cpu, cpu->register_y);
}

// Increment then subtract with carry
static void cpu_execute_isc(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_memory_read_byte(cpu, pointer) + 1;
    cpu_memory_write_byte(cpu, pointer, new_value);
    adc(cpu, ~new_value);
}

// Bit test
static void cpu_execute_bit(Cpu *cpu, AddressingMode addressing_mode) {
    uint8_t operand = cpu_decode_operand(cpu, addressing_mode);

    if ((operand & cpu->accumulator) == 0) {
        cpu_status_set_zero(cpu);
    } else {
        cpu_status_clear_zero(cpu);
    }

    if (operand & (1 << 6)) {
        cpu_status_set_overflow(cpu);
    } else {
        cpu_status_clear_overflow(cpu);
    }

    if (operand & (1 << 7)) {
        cpu_status_set_negative(cpu);
    } else {
        cpu_status_clear_negative(cpu);
    }
}

// Arithmetic shift left
static void cpu_execute_asl(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t operand_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t operand = cpu_memory_read_byte(cpu, operand_pointer);

    if (operand & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand <<= 1;

    cpu_memory_write_byte(cpu, operand_pointer, operand);

    cpu_status_update_zero_and_negative(cpu, operand);
}

// Logical shift right
static void cpu_execute_lsr(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t operand_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t operand = cpu_memory_read_byte(cpu, operand_pointer);

    if (operand & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand >>= 1;

    cpu_memory_write_byte(cpu, operand_pointer, operand);

    cpu_status_update_zero_and_negative(cpu, operand);
}

// Arithmetic shift left then perform logical or with accumulator
static void cpu_execute_slo(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t operand_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t operand = cpu_memory_read_byte(cpu, operand_pointer);

    if (operand & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand <<= 1;

    cpu_memory_write_byte(cpu, operand_pointer, operand);

    cpu->accumulator |= operand;

    cpu_status_update_zero_and_negative(cpu, cpu->accumulator);
}

// Jump
static void cpu_execute_jmp(Cpu *cpu, AddressingMode addressing_mode) {
    cpu->instruction_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
}

// Store accumulator
static void cpu_execute_sta(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_memory_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode),
                          cpu->accumulator);
}

// Load accumulator
static void cpu_execute_lda(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(
        cpu, cpu->accumulator = cpu_decode_operand(cpu, addressing_mode));
}

// Store X register
static void cpu_execute_stx(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_memory_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode),
                          cpu->register_x);
}

// Load X register
static void cpu_execute_ldx(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(
        cpu, cpu->register_x = cpu_decode_operand(cpu, addressing_mode));
}

// Store Y register
static void cpu_execute_sty(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_memory_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode),
                          cpu->register_y);
}

// Load Y register
static void cpu_execute_ldy(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(
        cpu, cpu->register_y = cpu_decode_operand(cpu, addressing_mode));
}

// No-op
static void cpu_execute_nop(Cpu *cpu) { (void)cpu; }

#define CHECK_INSTRUCTION(code, call, offset)                                  \
    case code + offset:                                                        \
        call(cpu);                                                             \
        break

#define CHECK_INSTRUCTION_WITH_AM(code, call, offset, addressing_mode)         \
    case code + offset:                                                        \
        call(cpu, addressing_mode);                                            \
        break

#define CHECK_ALU_INSTRUCTION_NO_IMM(code, call)                               \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x5, AM_ZERO_PAGE);                  \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x15, AM_ZERO_PAGE_X);               \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0xd, AM_ABSOLUTE);                   \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x1d, AM_ABSOLUTE_X);                \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x19, AM_ABSOLUTE_Y);                \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x1, AM_INDIRECT_X);                 \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x11, AM_INDIRECT_Y)

#define CHECK_ALU_INSTRUCTION(code, call)                                      \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x9, AM_IMMEDIATE);                  \
    CHECK_ALU_INSTRUCTION_NO_IMM(code, call)

#define CHECK_RMW_INSTRUCTION(code, call)                                      \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x6, AM_ZERO_PAGE);                  \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x16, AM_ZERO_PAGE_X);               \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0xe, AM_ABSOLUTE);                   \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x1e, AM_ABSOLUTE_X);                \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0xa, AM_ACCUMULATOR);

static void cpu_execute_instruction(Cpu *cpu) {
    uint8_t instruction = cpu_memory_read_byte(cpu, cpu->instruction_pointer);

    cpu->instruction_pointer++;

    switch (instruction) {
        CHECK_INSTRUCTION(OP_BRK, cpu_status_set_break, 0x00);
        CHECK_INSTRUCTION(OP_PHP, cpu_execute_php, 0x08);
        CHECK_INSTRUCTION(OP_PLP, cpu_execute_plp, 0x08);
        CHECK_INSTRUCTION(OP_PHA, cpu_execute_pha, 0x08);
        CHECK_INSTRUCTION(OP_PLA, cpu_execute_pla, 0x08);
        CHECK_INSTRUCTION(OP_JSR, cpu_execute_jsr, 0x00);
        CHECK_INSTRUCTION(OP_RTS, cpu_execute_rts, 0x00);

        CHECK_ALU_INSTRUCTION(OP_ADC, cpu_execute_adc);
        CHECK_ALU_INSTRUCTION(OP_SBC, cpu_execute_sbc);
        CHECK_ALU_INSTRUCTION(OP_AND, cpu_execute_and);
        CHECK_ALU_INSTRUCTION(OP_ORA, cpu_execute_ora);
        CHECK_ALU_INSTRUCTION(OP_EOR, cpu_execute_eor);
        CHECK_ALU_INSTRUCTION(OP_CMP, cpu_execute_cmp);

        CHECK_ALU_INSTRUCTION_NO_IMM(OP_STA, cpu_execute_sta);
        CHECK_ALU_INSTRUCTION(OP_LDA, cpu_execute_lda);

        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x02, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x16,
                                  AM_ZERO_PAGE_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x0e, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x1e, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x00, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x0c, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x14,
                                  AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x1c, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_STX, cpu_execute_stx, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_STX, cpu_execute_stx, 0x16,
                                  AM_ZERO_PAGE_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_STX, cpu_execute_stx, 0x0e, AM_ABSOLUTE);

        CHECK_INSTRUCTION_WITH_AM(OP_STY, cpu_execute_sty, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_STY, cpu_execute_sty, 0x14,
                                  AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_STY, cpu_execute_sty, 0x0c, AM_ABSOLUTE);

        CHECK_RMW_INSTRUCTION(OP_ASL, cpu_execute_asl);
        CHECK_RMW_INSTRUCTION(OP_LSR, cpu_execute_lsr);

        CHECK_INSTRUCTION_WITH_AM(OP_JMP, cpu_execute_jmp, 0x40, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_JMP, cpu_execute_jmp, 0x60,
                                  AM_INDIRECT_JMP);

        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x0e, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x16,
                                  AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x1e, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION(OP_INX, cpu_execute_inx, 0x08);
        CHECK_INSTRUCTION(OP_INY, cpu_execute_iny, 0x08);

        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x16,
                                  AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x0e, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x1e, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION(OP_DEX, cpu_execute_dex, 0x0a);
        CHECK_INSTRUCTION(OP_DEY, cpu_execute_dey, 0x08);

        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_SBC, cpu_execute_isc, 0x0b, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x17,
                                  AM_ZERO_PAGE_X);

        CHECK_INSTRUCTION_WITH_AM(OP_BIT, cpu_execute_bit, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_BIT, cpu_execute_bit, 0x0c, AM_ABSOLUTE);

        CHECK_INSTRUCTION(OP_BPL, cpu_execute_bpl, 0x10);
        CHECK_INSTRUCTION(OP_BMI, cpu_execute_bmi, 0x10);
        CHECK_INSTRUCTION(OP_BVC, cpu_execute_bvc, 0x10);
        CHECK_INSTRUCTION(OP_BVS, cpu_execute_bvs, 0x10);
        CHECK_INSTRUCTION(OP_BCC, cpu_execute_bcc, 0x10);
        CHECK_INSTRUCTION(OP_BCS, cpu_execute_bcs, 0x10);
        CHECK_INSTRUCTION(OP_BNE, cpu_execute_bne, 0x10);
        CHECK_INSTRUCTION(OP_BEQ, cpu_execute_beq, 0x10);

        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x1b, AM_ABSOLUTE_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x1f, AM_ABSOLUTE_X);

        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x07, AM_ZERO_PAGE);

        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x17,
                                  AM_ZERO_PAGE_X);

        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x1f, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x1b, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION(OP_NOP, cpu_execute_nop, 0x00);

    default:
        printf("error: unknown instruction with code: 0x%x\n", instruction);
        exit(1);
    }
}

void cpu_sync(Cpu *cpu, uint16_t master_clock) {
    while (!cpu_status_is_break(cpu) && cpu->internal_clock < master_clock) {
        cpu_execute_instruction(cpu);

        cpu->internal_clock++;
    }
}
