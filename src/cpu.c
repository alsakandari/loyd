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
static void cpu_status_disable_interrupts(Cpu *cpu) { cpu->status |= (1 << 2); }
static void cpu_status_enable_interrupts(Cpu *cpu) { cpu->status &= ~(1 << 2); }
static void cpu_status_set_decimal_mode(Cpu *cpu) { cpu->status |= (1 << 3); }
static void cpu_status_clear_decimal_mode(Cpu *cpu) { cpu->status &= ~(1 << 3); }
static void cpu_status_set_overflow(Cpu *cpu) { cpu->status |= (1 << 6); }
static void cpu_status_clear_overflow(Cpu *cpu) { cpu->status &= ~(1 << 6); }
static void cpu_status_set_negative(Cpu *cpu) { cpu->status |= (1 << 7); }
static void cpu_status_clear_negative(Cpu *cpu) { cpu->status &= ~(1 << 7); }

static bool cpu_status_is_carry(Cpu *cpu) { return cpu->status & 1; }
static bool cpu_status_is_zero(Cpu *cpu) { return (cpu->status & (1 << 1)) != 0; }
static bool cpu_status_is_negative(Cpu *cpu) { return (cpu->status & (1 << 7)) != 0; }
static bool cpu_status_is_overflow(Cpu *cpu) { return (cpu->status & (1 << 6)) != 0; }

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

bool cpu_stopped(Cpu *cpu) { return (cpu->status & (1 << 4)) != 0; }
static void cpu_stop(Cpu *cpu) { cpu->status |= (1 << 4); }
static void cpu_start(Cpu *cpu) { cpu->status &= ~(1 << 4); }

void cpu_power_on(Cpu *cpu) {
    cpu->stack_pointer = 0xFD;
    cpu->status = 0x34;
    cpu->instruction_pointer = cpu->accumulator = cpu->register_x = cpu->register_y = 0;
}

void cpu_load_rom(Cpu *cpu, const char *path) {
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "error: could not open file '%s': %s\n", path, strerror(errno));

        exit(1);
    }

    uint8_t magic[4];
    read_bytes_into(magic, file, path, 4);

    uint8_t expected_magic[4] = {'N', 'E', 'S', 0x1a};

    if (memcmp(magic, expected_magic, 4) != 0) {
        fprintf(stderr, "error: invalid magic: expected '%d', got '%d'\n", *(uint32_t *)expected_magic,
                *(uint32_t *)magic);

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

    memset(cpu->ram, 0, RAM_SIZE);

    cpu->mapper.map_ram(cpu->mapper.context, cpu->ram);

    cpu->instruction_pointer = cpu->mapper.description(cpu->mapper.context).instruction_pointer;

    cpu_start(cpu);
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

static inline void cpu_write_byte(Cpu *cpu, uint16_t pointer, uint8_t byte) {
    mirror_pointer(&pointer);

    if (is_io_register(pointer)) {
        return;
    }

    if (cpu->mapper.register_write != NULL) {
        MapperDesription mapper_description = cpu->mapper.description(cpu->mapper.context);

        if (mapper_description.registers_start >= pointer && pointer < mapper_description.registers_end) {
            cpu->mapper.register_write(cpu->mapper.context, pointer, byte);

            return;
        }
    }

    cpu->ram[pointer] = byte;
}

static inline uint8_t cpu_read_byte(Cpu *cpu, uint16_t pointer) {
    mirror_pointer(&pointer);

    if (is_io_register(pointer)) {
        return 0;
    }

    return cpu->ram[pointer];
}

static inline uint16_t cpu_read_word(Cpu *cpu, uint16_t pointer) {
    uint16_t lsb = cpu_read_byte(cpu, pointer);
    uint16_t hsb = cpu_read_byte(cpu, pointer + 1);

    return (hsb << 8) | lsb;
}

static inline uint8_t cpu_decode_byte(Cpu *cpu) { return cpu_read_byte(cpu, cpu->instruction_pointer++); }

static inline uint16_t cpu_decode_word(Cpu *cpu) {
    uint16_t word = cpu_read_word(cpu, cpu->instruction_pointer);
    cpu->instruction_pointer += 2;
    return word;
}

static uint16_t cpu_decode_operand_pointer(Cpu *cpu, AddressingMode addressing_mode) {
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
            return cpu_read_byte(cpu, pointer) + (((uint16_t)cpu_read_byte(cpu, pointer & 0xff00)) << 8);
        } else {
            return cpu_read_word(cpu, pointer);
        }
    }
    case AM_INDIRECT_X:
        return cpu_read_word(cpu, cpu_decode_byte(cpu) + cpu->register_x);
    case AM_INDIRECT_Y:
        return cpu_read_word(cpu, cpu_decode_byte(cpu)) + cpu->register_y;

    default:
        printf("error: unknown addressing mode: %d\n", addressing_mode);
        exit(1);

        break;
    }
}

static inline uint8_t cpu_decode_operand(Cpu *cpu, AddressingMode addressing_mode) {
    return cpu_read_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode));
}

static inline void cpu_push_byte(Cpu *cpu, uint8_t byte) { cpu_write_byte(cpu, cpu->stack_pointer--, byte); }

static inline uint8_t cpu_pull_byte(Cpu *cpu) { return cpu_read_byte(cpu, ++cpu->stack_pointer); }

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
    if ((((old_lhs & (1 << 7)) == (lhs & (1 << 7))) && ((old_lhs & (1 << 7)) != (rhs & (1 << 7))))) {
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
static void cpu_execute_bmi(Cpu *cpu) { branch(cpu, cpu_status_is_negative(cpu)); }

// Branch if positive
static void cpu_execute_bpl(Cpu *cpu) { branch(cpu, !cpu_status_is_negative(cpu)); }

// Branch if carry set
static void cpu_execute_bcs(Cpu *cpu) { branch(cpu, cpu_status_is_carry(cpu)); }

// Branch if carry clear
static void cpu_execute_bcc(Cpu *cpu) { branch(cpu, !cpu_status_is_carry(cpu)); }

// Branch if overflow set
static void cpu_execute_bvs(Cpu *cpu) { branch(cpu, cpu_status_is_overflow(cpu)); }

// Branch if overflow clear
static void cpu_execute_bvc(Cpu *cpu) { branch(cpu, !cpu_status_is_overflow(cpu)); }

// Branch if equal
static void cpu_execute_beq(Cpu *cpu) { branch(cpu, cpu_status_is_zero(cpu)); }

// Branch if not equal
static void cpu_execute_bne(Cpu *cpu) { branch(cpu, !cpu_status_is_zero(cpu)); }

// Push processor status
static void cpu_execute_php(Cpu *cpu) { cpu_push_byte(cpu, cpu->status | 0x30); }

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
static void cpu_execute_rts(Cpu *cpu) { cpu->instruction_pointer = cpu_pull_word(cpu) + 1; }

// Add with carry
static void cpu_execute_adc(Cpu *cpu, AddressingMode addressing_mode) {
    adc(cpu, cpu_decode_operand(cpu, addressing_mode));
}

// Subtract with carry
static void cpu_execute_sbc(Cpu *cpu, AddressingMode addressing_mode) {
    adc(cpu, ~cpu_decode_operand(cpu, addressing_mode));
}

// Rotate left
static void cpu_execute_rol(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);

    uint8_t old_value = cpu_read_byte(cpu, pointer);

    uint8_t new_value = (old_value << 1) | (uint8_t)cpu_status_is_carry(cpu);

    cpu_write_byte(cpu, pointer, new_value);

    if (old_value & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    cpu_status_update_zero_and_negative(cpu, new_value);
}

// Rotate right
static void cpu_execute_ror(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);

    uint8_t old_value = cpu_read_byte(cpu, pointer);

    uint8_t new_value = (old_value >> 1) | ((uint8_t)cpu_status_is_carry(cpu) << 7);

    cpu_write_byte(cpu, pointer, new_value);

    if (old_value & 1) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    cpu_status_update_zero_and_negative(cpu, new_value);
}

// Rotate left then perform Logical And on the value
static void cpu_execute_rla(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);

    uint8_t old_value = cpu_read_byte(cpu, pointer);

    uint8_t new_value = (old_value << 1) | (uint8_t)cpu_status_is_carry(cpu);

    cpu_write_byte(cpu, pointer, new_value);

    if (old_value & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    cpu_status_update_zero_and_negative(cpu, cpu->accumulator &= new_value);
}

// Rotate left then perform Add with Carry on the value
static void cpu_execute_rra(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);

    uint8_t old_value = cpu_read_byte(cpu, pointer);

    uint8_t new_value = (old_value << 1) | (uint8_t)cpu_status_is_carry(cpu);

    cpu_write_byte(cpu, pointer, new_value);

    if (old_value & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    adc(cpu, new_value);
}

// Logical And with accumulator and register X
static void cpu_execute_sax(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode), cpu->accumulator & cpu->register_x);
}

// Load into accumulator and then transfer to register X
static void cpu_execute_lax(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->register_x = cpu->accumulator =
                                                 cpu_decode_operand(cpu, addressing_mode));
}

// Logical And
static void cpu_execute_and(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator &= cpu_decode_operand(cpu, addressing_mode));
}

// Logical Or
static void cpu_execute_ora(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator |= cpu_decode_operand(cpu, addressing_mode));
}

// Exclusive Or
static void cpu_execute_eor(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator ^= cpu_decode_operand(cpu, addressing_mode));
}

// Compare lhs with rhs
static void cmp(Cpu *cpu, uint8_t lhs, uint8_t rhs) {
    uint8_t diff = lhs - rhs;

    if (lhs >= rhs) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    cpu_status_update_zero_and_negative(cpu, diff);
}

// Decrement value and then compare
static void cpu_execute_dcp(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_read_byte(cpu, pointer) - 1;
    cpu_write_byte(cpu, pointer, new_value);
    cpu_status_update_zero_and_negative(cpu, new_value);
    cmp(cpu, cpu->accumulator, new_value);
}


// Compare accumulator with operand
static void cpu_execute_cmp(Cpu *cpu, AddressingMode addressing_mode) {
    cmp(cpu, cpu->accumulator, cpu_decode_operand(cpu, addressing_mode));
}

// Compare register X with operand
static void cpu_execute_cpx(Cpu *cpu, AddressingMode addressing_mode) {
    cmp(cpu, cpu->register_x, cpu_decode_operand(cpu, addressing_mode));
}

// Compare register Y with operand
static void cpu_execute_cpy(Cpu *cpu, AddressingMode addressing_mode) {
    cmp(cpu, cpu->register_y, cpu_decode_operand(cpu, addressing_mode));
}

// Increment
static void cpu_execute_inc(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_read_byte(cpu, pointer) + 1;
    cpu_write_byte(cpu, pointer, new_value);
    cpu_status_update_zero_and_negative(cpu, new_value);
}

// Increment X
static void cpu_execute_inx(Cpu *cpu) { cpu_status_update_zero_and_negative(cpu, ++cpu->register_x); }

// Increment Y
static void cpu_execute_iny(Cpu *cpu) { cpu_status_update_zero_and_negative(cpu, ++cpu->register_y); }

// Decrement
static void cpu_execute_dec(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_read_byte(cpu, pointer) - 1;
    cpu_write_byte(cpu, pointer, new_value);
    cpu_status_update_zero_and_negative(cpu, new_value);
}

// Decrement X
static void cpu_execute_dex(Cpu *cpu) { cpu_status_update_zero_and_negative(cpu, --cpu->register_x); }

// Decrement Y
static void cpu_execute_dey(Cpu *cpu) { cpu_status_update_zero_and_negative(cpu, --cpu->register_y); }

// Increment then subtract with carry
static void cpu_execute_isc(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t new_value = cpu_read_byte(cpu, pointer) + 1;
    cpu_write_byte(cpu, pointer, new_value);
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
    uint8_t operand = cpu_read_byte(cpu, operand_pointer);

    if (operand & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand <<= 1;

    cpu_write_byte(cpu, operand_pointer, operand);

    cpu_status_update_zero_and_negative(cpu, operand);
}

// Logical shift right
static void cpu_execute_lsr(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t operand_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t operand = cpu_read_byte(cpu, operand_pointer);

    if (operand & 1) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand >>= 1;

    cpu_write_byte(cpu, operand_pointer, operand);

    cpu_status_update_zero_and_negative(cpu, operand);
}

// Arithmetic shift left then perform Logical Or with accumulator
static void cpu_execute_slo(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t operand_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t operand = cpu_read_byte(cpu, operand_pointer);

    if (operand & (1 << 7)) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand <<= 1;

    cpu_write_byte(cpu, operand_pointer, operand);

    cpu_status_update_zero_and_negative(cpu, cpu->accumulator |= operand);
}

// Logical shift right then perform perform Logical Exclusive Or with the value
static void cpu_execute_sre(Cpu *cpu, AddressingMode addressing_mode) {
    uint16_t operand_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
    uint8_t operand = cpu_read_byte(cpu, operand_pointer);

    if (operand & 1) {
        cpu_status_set_carry(cpu);
    } else {
        cpu_status_clear_carry(cpu);
    }

    operand >>= 1;

    cpu_write_byte(cpu, operand_pointer, operand);

    cpu_status_update_zero_and_negative(cpu, cpu->accumulator ^= operand);
}

// Jump
static void cpu_execute_jmp(Cpu *cpu, AddressingMode addressing_mode) {
    cpu->instruction_pointer = cpu_decode_operand_pointer(cpu, addressing_mode);
}

// Store accumulator
static void cpu_execute_sta(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode), cpu->accumulator);
}

// Load accumulator
static void cpu_execute_lda(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator = cpu_decode_operand(cpu, addressing_mode));
}

// Store X register
static void cpu_execute_stx(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode), cpu->register_x);
}

// Load X register
static void cpu_execute_ldx(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->register_x = cpu_decode_operand(cpu, addressing_mode));
}

// Store Y register
static void cpu_execute_sty(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_write_byte(cpu, cpu_decode_operand_pointer(cpu, addressing_mode), cpu->register_y);
}

// Load Y register
static void cpu_execute_ldy(Cpu *cpu, AddressingMode addressing_mode) {
    cpu_status_update_zero_and_negative(cpu, cpu->register_y = cpu_decode_operand(cpu, addressing_mode));
}

// Transfer accumulator to register X
static void cpu_execute_tax(Cpu *cpu) {
    cpu_status_update_zero_and_negative(cpu, cpu->register_x = cpu->accumulator);
}

// Transfer accumulator to register Y
static void cpu_execute_tay(Cpu *cpu) {
    cpu_status_update_zero_and_negative(cpu, cpu->register_y = cpu->accumulator);
}

// Transfer stack pointer to register X
static void cpu_execute_tsx(Cpu *cpu) {
    cpu_status_update_zero_and_negative(cpu, cpu->register_x = cpu->stack_pointer);
}

// Transfer register X to stack pointer
static void cpu_execute_txs(Cpu *cpu) { cpu->stack_pointer = cpu->register_x; }

// Transfer register X to accumulator
static void cpu_execute_txa(Cpu *cpu) {
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator = cpu->register_x);
}

// Transfer register Y to accumulator
static void cpu_execute_tya(Cpu *cpu) {
    cpu_status_update_zero_and_negative(cpu, cpu->accumulator = cpu->register_y);
}

// No-op
static void cpu_execute_nop(Cpu *cpu, AddressingMode addressing_mode) {
    if (addressing_mode != AM_IMPLICIT) {
        cpu_decode_operand(cpu, addressing_mode);
    }
}

#define CHECK_INSTRUCTION(code, call)                                                                        \
    case code:                                                                                               \
        call(cpu);                                                                                           \
        break

#define CHECK_INSTRUCTION_WITH_AM(code, call, offset, addressing_mode)                                       \
    case code + offset:                                                                                      \
        call(cpu, addressing_mode);                                                                          \
        break

#define CHECK_ALU_INSTRUCTION_NO_IMM(code, call)                                                             \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x5, AM_ZERO_PAGE);                                                \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x15, AM_ZERO_PAGE_X);                                             \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0xd, AM_ABSOLUTE);                                                 \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x1d, AM_ABSOLUTE_X);                                              \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x19, AM_ABSOLUTE_Y);                                              \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x1, AM_INDIRECT_X);                                               \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x11, AM_INDIRECT_Y);

#define CHECK_ALU_INSTRUCTION(code, call)                                                                    \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x9, AM_IMMEDIATE);                                                \
    CHECK_ALU_INSTRUCTION_NO_IMM(code, call);

#define CHECK_RMW_INSTRUCTION(code, call)                                                                    \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x6, AM_ZERO_PAGE);                                                \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x16, AM_ZERO_PAGE_X);                                             \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0xe, AM_ABSOLUTE);                                                 \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0x1e, AM_ABSOLUTE_X);                                              \
    CHECK_INSTRUCTION_WITH_AM(code, call, 0xa, AM_ACCUMULATOR);

static void cpu_execute_instruction(Cpu *cpu) {
    uint8_t instruction = cpu_read_byte(cpu, cpu->instruction_pointer);

    cpu->instruction_pointer++;

    switch (instruction) {
        CHECK_INSTRUCTION(OP_PHP, cpu_execute_php);
        CHECK_INSTRUCTION(OP_PLP, cpu_execute_plp);
        CHECK_INSTRUCTION(OP_PHA, cpu_execute_pha);
        CHECK_INSTRUCTION(OP_PLA, cpu_execute_pla);
        CHECK_INSTRUCTION(OP_JSR, cpu_execute_jsr);
        CHECK_INSTRUCTION(OP_RTS, cpu_execute_rts);

        CHECK_ALU_INSTRUCTION(OP_ADC, cpu_execute_adc);
        CHECK_ALU_INSTRUCTION(OP_SBC, cpu_execute_sbc);
        CHECK_ALU_INSTRUCTION(OP_AND, cpu_execute_and);
        CHECK_ALU_INSTRUCTION(OP_ORA, cpu_execute_ora);
        CHECK_ALU_INSTRUCTION(OP_EOR, cpu_execute_eor);
        CHECK_ALU_INSTRUCTION(OP_CMP, cpu_execute_cmp);
        CHECK_ALU_INSTRUCTION_NO_IMM(OP_STA, cpu_execute_sta);
        CHECK_ALU_INSTRUCTION(OP_LDA, cpu_execute_lda);

        CHECK_RMW_INSTRUCTION(OP_ASL, cpu_execute_asl);
        CHECK_RMW_INSTRUCTION(OP_ROL, cpu_execute_rol);
        CHECK_RMW_INSTRUCTION(OP_LSR, cpu_execute_lsr);
        CHECK_RMW_INSTRUCTION(OP_ROR, cpu_execute_ror);

        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x02, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x16, AM_ZERO_PAGE_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x0e, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDX, cpu_execute_ldx, 0x1e, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x00, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x0c, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x14, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_LDY, cpu_execute_ldy, 0x1c, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_STX, cpu_execute_stx, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_STX, cpu_execute_stx, 0x16, AM_ZERO_PAGE_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_STX, cpu_execute_stx, 0x0e, AM_ABSOLUTE);

        CHECK_INSTRUCTION_WITH_AM(OP_STY, cpu_execute_sty, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_STY, cpu_execute_sty, 0x14, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_STY, cpu_execute_sty, 0x0c, AM_ABSOLUTE);

        CHECK_INSTRUCTION_WITH_AM(OP_CPX, cpu_execute_cpx, 0x00, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_CPX, cpu_execute_cpx, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_CPX, cpu_execute_cpx, 0x0c, AM_ABSOLUTE);

        CHECK_INSTRUCTION_WITH_AM(OP_CPY, cpu_execute_cpy, 0x00, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_CPY, cpu_execute_cpy, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_CPY, cpu_execute_cpy, 0x0c, AM_ABSOLUTE);

        CHECK_INSTRUCTION(OP_TAX, cpu_execute_tax);
        CHECK_INSTRUCTION(OP_TAY, cpu_execute_tay);
        CHECK_INSTRUCTION(OP_TSX, cpu_execute_tsx);
        CHECK_INSTRUCTION(OP_TXA, cpu_execute_txa);
        CHECK_INSTRUCTION(OP_TXS, cpu_execute_txs);
        CHECK_INSTRUCTION(OP_TYA, cpu_execute_tya);

        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x0e, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x16, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_INC, cpu_execute_inc, 0x1e, AM_ABSOLUTE_X);

        CHECK_INSTRUCTION(OP_INX, cpu_execute_inx);
        CHECK_INSTRUCTION(OP_INY, cpu_execute_iny);

        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x06, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x16, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x0e, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_DEC, cpu_execute_dec, 0x1e, AM_ABSOLUTE_X);

        CHECK_INSTRUCTION(OP_DEX, cpu_execute_dex);
        CHECK_INSTRUCTION(OP_DEY, cpu_execute_dey);

        CHECK_INSTRUCTION(OP_SEC, cpu_status_set_carry);
        CHECK_INSTRUCTION(OP_SED, cpu_status_set_decimal_mode);
        CHECK_INSTRUCTION(OP_SEI, cpu_status_disable_interrupts);
        CHECK_INSTRUCTION(OP_CLC, cpu_status_clear_carry);
        CHECK_INSTRUCTION(OP_CLD, cpu_status_clear_decimal_mode);
        CHECK_INSTRUCTION(OP_CLI, cpu_status_enable_interrupts);
        CHECK_INSTRUCTION(OP_CLV, cpu_status_clear_overflow);

        CHECK_INSTRUCTION_WITH_AM(OP_JMP, cpu_execute_jmp, 0x40, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_JMP, cpu_execute_jmp, 0x60, AM_INDIRECT_JMP);

        CHECK_INSTRUCTION_WITH_AM(OP_BIT, cpu_execute_bit, 0x04, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_BIT, cpu_execute_bit, 0x0c, AM_ABSOLUTE);

        CHECK_INSTRUCTION(OP_BPL, cpu_execute_bpl);
        CHECK_INSTRUCTION(OP_BMI, cpu_execute_bmi);
        CHECK_INSTRUCTION(OP_BVC, cpu_execute_bvc);
        CHECK_INSTRUCTION(OP_BVS, cpu_execute_bvs);
        CHECK_INSTRUCTION(OP_BCC, cpu_execute_bcc);
        CHECK_INSTRUCTION(OP_BCS, cpu_execute_bcs);
        CHECK_INSTRUCTION(OP_BNE, cpu_execute_bne);
        CHECK_INSTRUCTION(OP_BEQ, cpu_execute_beq);

        CHECK_INSTRUCTION(0x00, cpu_stop);
        CHECK_INSTRUCTION(0x02, cpu_stop);
        CHECK_INSTRUCTION(0x12, cpu_stop);
        CHECK_INSTRUCTION(0x22, cpu_stop);
        CHECK_INSTRUCTION(0x32, cpu_stop);
        CHECK_INSTRUCTION(0x42, cpu_stop);
        CHECK_INSTRUCTION(0x52, cpu_stop);
        CHECK_INSTRUCTION(0x62, cpu_stop);
        CHECK_INSTRUCTION(0x72, cpu_stop);
        CHECK_INSTRUCTION(0x92, cpu_stop);
        CHECK_INSTRUCTION(0xb2, cpu_stop);
        CHECK_INSTRUCTION(0xd2, cpu_stop);
        CHECK_INSTRUCTION(0xf2, cpu_stop);

        CHECK_INSTRUCTION_WITH_AM(0xea, cpu_execute_nop, 0x00, AM_IMPLICIT);
        CHECK_INSTRUCTION_WITH_AM(0x80, cpu_execute_nop, 0x00, AM_IMMEDIATE);

        CHECK_INSTRUCTION_WITH_AM(0x04, cpu_execute_nop, 0x00, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(0x44, cpu_execute_nop, 0x00, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(0x64, cpu_execute_nop, 0x00, AM_ZERO_PAGE);

        CHECK_INSTRUCTION_WITH_AM(0x0c, cpu_execute_nop, 0x00, AM_ABSOLUTE);

        CHECK_INSTRUCTION_WITH_AM(0x14, cpu_execute_nop, 0x00, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(0x34, cpu_execute_nop, 0x00, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(0x54, cpu_execute_nop, 0x00, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(0x74, cpu_execute_nop, 0x00, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(0xd4, cpu_execute_nop, 0x00, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(0xf4, cpu_execute_nop, 0x00, AM_ZERO_PAGE_X);

        CHECK_INSTRUCTION_WITH_AM(0x1c, cpu_execute_nop, 0x00, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(0x3c, cpu_execute_nop, 0x00, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(0x5c, cpu_execute_nop, 0x00, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(0x7c, cpu_execute_nop, 0x00, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(0xdc, cpu_execute_nop, 0x00, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(0xfc, cpu_execute_nop, 0x00, AM_ABSOLUTE_X);

        CHECK_INSTRUCTION_WITH_AM(0x89, cpu_execute_nop, 0x00, AM_IMMEDIATE);

        CHECK_INSTRUCTION_WITH_AM(0x82, cpu_execute_nop, 0x00, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(0xc2, cpu_execute_nop, 0x00, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(0xe2, cpu_execute_nop, 0x00, AM_IMMEDIATE);

        CHECK_INSTRUCTION_WITH_AM(0x1a, cpu_execute_nop, 0x00, AM_IMPLICIT);
        CHECK_INSTRUCTION_WITH_AM(0x3a, cpu_execute_nop, 0x00, AM_IMPLICIT);
        CHECK_INSTRUCTION_WITH_AM(0x5a, cpu_execute_nop, 0x00, AM_IMPLICIT);
        CHECK_INSTRUCTION_WITH_AM(0x7a, cpu_execute_nop, 0x00, AM_IMPLICIT);
        CHECK_INSTRUCTION_WITH_AM(0xda, cpu_execute_nop, 0x00, AM_IMPLICIT);
        CHECK_INSTRUCTION_WITH_AM(0xfa, cpu_execute_nop, 0x00, AM_IMPLICIT);

        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x1f, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SLO, cpu_execute_slo, 0x1b, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x1f, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_RLA, cpu_execute_rla, 0x1b, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x1f, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SRE, cpu_execute_sre, 0x1b, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x1f, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_RRA, cpu_execute_rra, 0x1b, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_SAX, cpu_execute_sax, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SAX, cpu_execute_sax, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_SAX, cpu_execute_sax, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_SAX, cpu_execute_sax, 0x0f, AM_ABSOLUTE);

        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x0b, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_LAX, cpu_execute_lax, 0x1f, AM_ABSOLUTE_X);

        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x1f, AM_ABSOLUTE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_DCP, cpu_execute_dcp, 0x1b, AM_ABSOLUTE_Y);

        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x03, AM_INDIRECT_X);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x07, AM_ZERO_PAGE);
        CHECK_INSTRUCTION_WITH_AM(OP_SBC, cpu_execute_sbc, 0x0b, AM_IMMEDIATE);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x0f, AM_ABSOLUTE);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x13, AM_INDIRECT_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x17, AM_ZERO_PAGE_X);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x1b, AM_ABSOLUTE_Y);
        CHECK_INSTRUCTION_WITH_AM(OP_ISC, cpu_execute_isc, 0x1f, AM_ABSOLUTE_X);
    default:
        printf("error: unknown instruction with code: 0x%x\n", instruction);
        exit(1);
    }
}

void cpu_sync(Cpu *cpu, uint16_t master_clock) {
    while (!cpu_stopped(cpu) && cpu->internal_clock < master_clock) {
        cpu_execute_instruction(cpu);

        cpu->internal_clock++;
    }
}
