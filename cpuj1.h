#ifndef CPUJ1_H
#define CPUJ1_H

#include <stdint.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════
 *                     cpuj1 — architecture v1
 *
 *  8-bit custom CPU. 4 registers (R0-R3), 256 bytes of RAM,
 *  16-bit instruction words, flags register (Z, C, N).
 *
 *  Stack lives at the top of RAM (0xF0-0xFF), growing downward.
 *  SP points to the next free slot; push pre-decrements, pop
 *  post-increments. Programs load at address 0x00.
 * ══════════════════════════════════════════════════════════════════ */

#define CPUJ1_RAM_SIZE      256
#define CPUJ1_REG_COUNT     4
#define CPUJ1_STACK_BOTTOM  0xF0
#define CPUJ1_STACK_TOP     0xFF
#define CPUJ1_PROGRAM_START 0x00

/* ── Registers ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  r[CPUJ1_REG_COUNT]; /* R0-R3 */
    uint8_t  sp;                 /* stack pointer */
    uint16_t pc;                 /* program counter */
    uint8_t  flags;              /* Z | C | N */
    uint8_t  ram[CPUJ1_RAM_SIZE];
    bool     halted;
} cpuj1_t;

#define CPUJ1_FLAG_Z (1 << 0)
#define CPUJ1_FLAG_C (1 << 1)
#define CPUJ1_FLAG_N (1 << 2)

/* ═══════════════════════════════════════════════════════════════════
 *   Instruction set, v1
 *
 *   Every instruction is exactly 16 bits.
 *
 *     Byte 0 (high): [ op:4 ][ Rd:2 ][ Rs:2 ]
 *     Byte 1 (low):  operand, layout depends on opcode (below)
 *
 *   Register field values 0..3 map to R0..R3.
 *
 *   ┌─────┬──────────────────────────────┬──────────────────────────┐
 *   │ OP  │ Assembly                     │ Byte 1 operand           │
 *   ├─────┼──────────────────────────────┼──────────────────────────┤
 *   │ 0   │ MOV Rd, Rs                   │ unused (0)               │
 *   │ 1   │ MOVI Rd, #imm                │ imm8                     │
 *   │ 2   │ ADD Rd, Rs | ADD Rd, #imm    │ imm8 or 0 (reg mode)     │
 *   │ 3   │ SUB Rd, Rs | SUB Rd, #imm    │ imm8 or 0 (reg mode)     │
 *   │ 4   │ AND Rd, Rs | AND Rd, #imm    │ imm8 or 0 (reg mode)     │
 *   │ 5   │ OR  Rd, Rs | OR  Rd, #imm    │ imm8 or 0 (reg mode)     │
 *   │ 6   │ XOR Rd, Rs | XOR Rd, #imm    │ imm8 or 0 (reg mode)     │
 *   │ 7   │ NOT Rd                       │ unused (0)               │
 *   │ 8   │ SHL Rd, #n                   │ 0x0n (n = 0..15)         │
 *   │ 9   │ SHR Rd, #n                   │ 0x0n (n = 0..15)         │
 *   │ A   │ CMP Rd, Rs | CMP Rd, #imm    │ imm8 or 0 (reg mode)     │
 *   │ B   │ LD Rd, [addr8] | LD Rd, [Ri] │ addr8; 0x01 + Ri => ptr  │
 *   │ C   │ ST [addr8], Rs | ST [Ri], Rs │ addr8; 0x01 + Ri => ptr  │
 *   │ D   │ JMP/JEQ/JNE/JGT addr8        │ addr8, cond in Rs field  │
 *   │ E   │ MISC (PUSH/POP/TRAP/... )    │ see below                │
 *   │ F   │ CALL addr8                   │ addr8                    │
 *   └─────┴──────────────────────────────┴──────────────────────────┘
 *
 *   Register vs. immediate for the ALU ops (ADD/SUB/AND/OR/XOR/CMP):
 *   the assembler resolves this. If the source is a register R0-R2, the
 *   Rs field carries it and byte1 is 0. If it's an immediate, Rs field
 *   is set to 3 (R3 is used only as a marker; real register-operand
 *   ALU with R3 must be written as reg-reg with a temp, or the assembler
 *   rejects it) and byte1 holds the immediate.
 *
 *   MISC (0xE), byte 1:
 *     0x00           PUSH Rs     (Rs from byte 0)
 *     0x20           POP  Rd     (Rd from byte 0)
 *     0x30           RET
 *     0x40 | code    TRAP code   (code 0..15)
 *     0x50           HALT
 *     0x60           NOP
 *
 *   JMP (0xD), condition in Rs field:
 *     0 = JMP (always), 1 = JEQ (Z), 2 = JNE (!Z), 3 = JGT (!(Z|N))
 * ══════════════════════════════════════════════════════════════════ */

typedef enum {
    OP_MOV  = 0x0,
    OP_MOVI = 0x1,
    OP_ADD  = 0x2,
    OP_SUB  = 0x3,
    OP_AND  = 0x4,
    OP_OR   = 0x5,
    OP_XOR  = 0x6,
    OP_NOT  = 0x7,
    OP_SHL  = 0x8,
    OP_SHR  = 0x9,
    OP_CMP  = 0xA,
    OP_LD   = 0xB,
    OP_ST   = 0xC,
    OP_JMP  = 0xD,
    OP_MISC = 0xE,
    OP_CALL = 0xF
} cpuj1_opcode_t;

/* Rs field marker meaning "byte1 is an immediate" for ALU ops */
#define CPUJ1_RS_IMM 3

typedef enum {
    MISC_PUSH = 0x00,
    MISC_POP  = 0x20,
    MISC_RET  = 0x30,
    MISC_TRAP = 0x40,
    MISC_HALT = 0x50,
    MISC_NOP  = 0x60
} cpuj1_misc_t;

typedef enum {
    JMP_ALWAYS = 0x0,
    JMP_EQ     = 0x1,
    JMP_NE     = 0x2,
    JMP_GT     = 0x3
} cpuj1_jump_t;

typedef enum {
    TRAP_PRINT_REG  = 0,  /* print R0 as unsigned decimal */
    TRAP_PRINT_CHAR = 1,  /* print R0 as ASCII char          */
    TRAP_READ_CHAR  = 2,  /* read a char into R0             */
    TRAP_PRINT_STR  = 3,  /* print null-terminated string at address R0 */
    TRAP_HALT       = 15  /* halt */
} cpuj1_trap_t;

/* ── Instruction constructors ─────────────────────────────────────── */

#define CPUJ1_ENC(op, rd, rs, imm8) \
    ((uint16_t)(((uint16_t)(op) << 12) | \
                ((uint16_t)(rd) << 10) | \
                ((uint16_t)(rs) << 8)  | \
                ((uint16_t)(imm8) & 0xFF)))

#define CPUJ1_MOV(rd, rs)     CPUJ1_ENC(OP_MOV,  rd, rs, 0)
#define CPUJ1_ADD_REG(rd, rs) CPUJ1_ENC(OP_ADD,  rd, rs, 0)
#define CPUJ1_SUB_REG(rd, rs) CPUJ1_ENC(OP_SUB,  rd, rs, 0)
#define CPUJ1_AND_REG(rd, rs) CPUJ1_ENC(OP_AND,  rd, rs, 0)
#define CPUJ1_OR_REG(rd, rs)  CPUJ1_ENC(OP_OR,   rd, rs, 0)
#define CPUJ1_XOR_REG(rd, rs) CPUJ1_ENC(OP_XOR,  rd, rs, 0)
#define CPUJ1_CMP_REG(rd, rs) CPUJ1_ENC(OP_CMP,  rd, rs, 0)

#define CPUJ1_MOVI(rd, imm)    CPUJ1_ENC(OP_MOVI, rd, 0, imm)
#define CPUJ1_ADD_IMM(rd, imm) CPUJ1_ENC(OP_ADD,  rd, CPUJ1_RS_IMM, imm)
#define CPUJ1_SUB_IMM(rd, imm) CPUJ1_ENC(OP_SUB,  rd, CPUJ1_RS_IMM, imm)
#define CPUJ1_AND_IMM(rd, imm) CPUJ1_ENC(OP_AND,  rd, CPUJ1_RS_IMM, imm)
#define CPUJ1_OR_IMM(rd, imm)  CPUJ1_ENC(OP_OR,   rd, CPUJ1_RS_IMM, imm)
#define CPUJ1_XOR_IMM(rd, imm) CPUJ1_ENC(OP_XOR,  rd, CPUJ1_RS_IMM, imm)
#define CPUJ1_CMP_IMM(rd, imm) CPUJ1_ENC(OP_CMP,  rd, CPUJ1_RS_IMM, imm)

#define CPUJ1_NOT(rd)    CPUJ1_ENC(OP_NOT, rd, 0, 0)
#define CPUJ1_SHL(rd, n) CPUJ1_ENC(OP_SHL, rd, 0, ((n) & 0x0F))
#define CPUJ1_SHR(rd, n) CPUJ1_ENC(OP_SHR, rd, 0, ((n) & 0x0F))

#define CPUJ1_LD_ABS(rd, addr) CPUJ1_ENC(OP_LD, rd, 0, addr)
#define CPUJ1_LD_PTR(rd, ri)   CPUJ1_ENC(OP_LD, rd, ri, 0x01)
#define CPUJ1_ST_ABS(addr, rs) CPUJ1_ENC(OP_ST, rs, 0, addr)
#define CPUJ1_ST_PTR(ri, rs)   CPUJ1_ENC(OP_ST, rs, ri, 0x01)

#define CPUJ1_PUSH(rs) CPUJ1_ENC(OP_MISC, 0, rs, MISC_PUSH)
#define CPUJ1_POP(rd)  CPUJ1_ENC(OP_MISC, rd, 0, MISC_POP)

#define CPUJ1_JMP(addr) CPUJ1_ENC(OP_JMP, 0, JMP_ALWAYS, addr)
#define CPUJ1_JEQ(addr) CPUJ1_ENC(OP_JMP, 0, JMP_EQ, addr)
#define CPUJ1_JNE(addr) CPUJ1_ENC(OP_JMP, 0, JMP_NE, addr)
#define CPUJ1_JGT(addr) CPUJ1_ENC(OP_JMP, 0, JMP_GT, addr)
#define CPUJ1_CALL(addr) CPUJ1_ENC(OP_CALL, 0, 0, addr)
#define CPUJ1_RET()     CPUJ1_ENC(OP_MISC, 0, 0, MISC_RET)

#define CPUJ1_TRAP(code) CPUJ1_ENC(OP_MISC, 0, 0, MISC_TRAP | ((code) & 0x0F))
#define CPUJ1_HALT()     CPUJ1_ENC(OP_MISC, 0, 0, MISC_HALT)
#define CPUJ1_NOP()      CPUJ1_ENC(OP_MISC, 0, 0, MISC_NOP)

/* ── CPU interface ────────────────────────────────────────────────── */

void     cpuj1_init(cpuj1_t *cpu);
void     cpuj1_reset(cpuj1_t *cpu);
void     cpuj1_tick(cpuj1_t *cpu);            /* fetch + execute one instr */
void     cpuj1_execute(cpuj1_t *cpu, uint16_t instr);

void     cpuj1_mem_write(cpuj1_t *cpu, uint16_t addr, uint8_t val);
uint8_t  cpuj1_mem_read(cpuj1_t *cpu, uint16_t addr);
void     cpuj1_push(cpuj1_t *cpu, uint8_t val);
uint8_t  cpuj1_pop(cpuj1_t *cpu);
uint8_t  cpuj1_alu(cpuj1_t *cpu, int op, uint8_t rd, uint8_t src);
void     cpuj1_trap(cpuj1_t *cpu, int code);

/* Disassembler: renders one instruction, returns bytes written */
int cpuj1_disassemble(uint16_t instr, char *buf, int bufsize);

#endif /* CPUJ1_H */