#ifndef CPUJ_H
#define CPUJ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ═══════════════════════════════════════════════════════════════════
 *                      cpuj — 32-bit virtual CPU
 *
 *  8 general-purpose registers (R0-R7), each 32 bits.
 *  4 GiB of addressable memory. Every instruction is one fixed 32-bit
 *  word, stored big-endian.
 *  Flags: Z, C, N, V.
 *
 *  Memory map:
 *    0x00000000 .. 0xFFFFFFEF   program + data
 *    0xFFFFFFF0 .. 0xFFFFFFFF   stack (grows down from 0xFFFFFFF0)
 *
 *  SP starts at 0xFFFFFFF0; push pre-decrements by 4, pop post-
 *  increments by 4. Programs load at 0x00000000.
 *
 *  The 4 GiB of RAM lives on the heap; use cpuj_init() which returns
 *  false if that allocation fails, and cpuj_free() when done.
 * ══════════════════════════════════════════════════════════════════ */

#define CPUJ_RAM_SIZE      ((size_t)0x100000000ULL) /* 4 GiB */
#define CPUJ_REG_COUNT     8
#define CPUJ_STACK_TOP     0xFFFFFFF0
#define CPUJ_STACK_BOTTOM  0xFFFFFFE0
#define CPUJ_PROGRAM_START 0x00000000

/* ── Register state ──────────────────────────────────────────────── */

typedef struct {
    uint32_t r[CPUJ_REG_COUNT]; /* R0-R7 */
    uint32_t sp;
    uint32_t pc;
    uint8_t  flags;             /* Z C N V */
    uint8_t *ram;               /* 4 GiB, allocated by cpuj_init */
    bool     halted;
} cpuj_t;

#define CPUJ_FLAG_Z (1 << 0)
#define CPUJ_FLAG_C (1 << 1)
#define CPUJ_FLAG_N (1 << 2)
#define CPUJ_FLAG_V (1 << 3)

/* ═══════════════════════════════════════════════════════════════════
 *   Instruction encoding
 *
 *   Every instruction is exactly one 32-bit word, big-endian in RAM:
 *
 *     [ op:6 ][ rd:3 ][ rs:3 ][ imm:20 ]
 *     bits 31..26, 25..23, 22..20, 19..0
 *
 *   imm is usually an unsigned 20-bit value; it is *signed* (20-bit,
 *   two's complement) only for branch relative offsets and LD/ST
 *   displacement fields.
 *
 *   MOVU Rd,#n loads the upper bits: Rd = (n & 0xFFF) << 20.  Pairing
 *   it with ORI builds any 32-bit constant, so all 4 GiB of addresses
 *   are reachable:
 *       MOVU R0, #0x123       ; R0 = 0x12300000
 *       ORI  R0, #0x4567      ; R0 = 0x12304567
 * ══════════════════════════════════════════════════════════════════ */

typedef enum {
    OP_MOV   = 0x00,  /* MOV  Rd, Rs            */
    OP_MOVI  = 0x01,  /* MOVI Rd, #imm20        */
    OP_MOVU  = 0x02,  /* MOVU Rd, #n (upper)    */
    OP_ADD   = 0x03,  /* ADD  Rd, Rs            */
    OP_ADDI  = 0x04,  /* ADDI Rd, #imm20        */
    OP_SUB   = 0x05,  /* SUB  Rd, Rs            */
    OP_SUBI  = 0x06,  /* SUBI Rd, #imm20        */
    OP_AND   = 0x07,  /* AND  Rd, Rs            */
    OP_ANDI  = 0x08,  /* ANDI Rd, #imm20        */
    OP_OR    = 0x09,  /* OR   Rd, Rs            */
    OP_ORI   = 0x0A,  /* ORI  Rd, #imm20        */
    OP_XOR   = 0x0B,  /* XOR  Rd, Rs            */
    OP_XORI  = 0x0C,  /* XORI Rd, #imm20        */
    OP_NOT   = 0x0D,  /* NOT  Rd                */
    OP_SHL   = 0x0E,  /* SHL  Rd, Rs (count)    */
    OP_SHLI  = 0x0F,  /* SHLI Rd, #n            */
    OP_SHR   = 0x10,  /* SHR  Rd, Rs (count)    */
    OP_SHRI  = 0x11,  /* SHRI Rd, #n            */
    OP_CMP   = 0x12,  /* CMP  Rd, Rs            */
    OP_CMPI  = 0x13,  /* CMPI Rd, #imm20        */
    OP_LD    = 0x14,  /* LD   Rd, [Rs+off]      */
    OP_ST    = 0x15,  /* ST   [Rs+off], Rd      */
    OP_LDB   = 0x16,  /* LDB  Rd, [Rs+off]      */
    OP_STB   = 0x17,  /* STB  [Rs+off], Rd      */
    OP_JMP   = 0x18,  /* JMP  rel20             */
    OP_JEQ   = 0x19,  /* JEQ  rel20             */
    OP_JNE   = 0x1A,  /* JNE  rel20             */
    OP_JGT   = 0x1B,  /* JGT  rel20 (as flags)  */
    OP_JGE   = 0x1C,  /* JGE  rel20             */
    OP_JLT   = 0x1D,  /* JLT  rel20             */
    OP_JLE   = 0x1E,  /* JLE  rel20             */
    OP_JMPR  = 0x1F,  /* JMPR Rs (indirect)     */
    OP_CALL  = 0x20,  /* CALL rel20             */
    OP_CALLR = 0x21,  /* CALLR Rs (indirect)    */
    OP_PUSH  = 0x22,  /* PUSH Rd                */
    OP_POP   = 0x23,  /* POP  Rd                */
    OP_RET   = 0x24,  /* RET                    */
    OP_TRAP  = 0x25,  /* TRAP code              */
    OP_HALT  = 0x26,  /* HALT                   */
    OP_NOP   = 0x27   /* NOP                    */
} cpuj_opcode_t;

/* Conditional branch predicates (map OP_JMP..OP_JLE to a condition). */
typedef enum {
    JMP_ALWAYS = 0,   /* matches OP_JMP   */
    JMP_EQ     = 1,   /* OP_JEQ          */
    JMP_NE     = 2,   /* OP_JNE          */
    JMP_GT     = 3,   /* OP_JGT          */
    JMP_GE     = 4,   /* OP_JGE          */
    JMP_LT     = 5,   /* OP_JLT          */
    JMP_LE     = 6    /* OP_JLE          */
} cpuj_jump_t;

typedef enum {
    TRAP_PRINT_REG  = 0,
    TRAP_PRINT_CHAR = 1,
    TRAP_READ_CHAR  = 2,
    TRAP_PRINT_STR  = 3,
    TRAP_HALT       = 6
} cpuj_trap_t;

/* Field accessors */
#define CPUJ_OP(i)   (((uint32_t)(i) >> 26) & 0x3F)
#define CPUJ_RD(i)   (((uint32_t)(i) >> 23) & 0x7)
#define CPUJ_RS(i)   (((uint32_t)(i) >> 20) & 0x7)
#define CPUJ_IMM(i)  ((uint32_t)(i) & 0xFFFFF)

/* imm as a signed 20-bit value (branches, LD/ST offsets) */
#define CPUJ_SIMM(i) ((int32_t)(((uint32_t)((i) & 0xFFFFF)) << 12) >> 12)

/* ── Constructors ───────────────────────────────────────────────── */

#define CPUJ_INS(op, rd, rs, imm) \
    ((uint32_t)(((uint32_t)(op) & 0x3F) << 26) | \
                (((uint32_t)(rd) & 0x7) << 23) | \
                (((uint32_t)(rs) & 0x7) << 20) | \
                ((uint32_t)(imm) & 0xFFFFF))

#define CPUJ_MOV(rd, rs)    CPUJ_INS(OP_MOV, rd, rs, 0)
#define CPUJ_MOVI(rd, imm)  CPUJ_INS(OP_MOVI, rd, 0, (imm) & 0xFFFFF)
#define CPUJ_MOVU(rd, n)    CPUJ_INS(OP_MOVU, rd, 0, (n) & 0xFFF)

#define CPUJ_ADD(rd, rs)   CPUJ_INS(OP_ADD,  rd, rs, 0)
#define CPUJ_SUB(rd, rs)   CPUJ_INS(OP_SUB,  rd, rs, 0)
#define CPUJ_AND(rd, rs)   CPUJ_INS(OP_AND,  rd, rs, 0)
#define CPUJ_OR(rd, rs)    CPUJ_INS(OP_OR,   rd, rs, 0)
#define CPUJ_XOR(rd, rs)   CPUJ_INS(OP_XOR,  rd, rs, 0)
#define CPUJ_CMP(rd, rs)   CPUJ_INS(OP_CMP,  rd, rs, 0)

#define CPUJ_ADDI(rd, imm) CPUJ_INS(OP_ADDI, rd, 0, (imm) & 0xFFFFF)
#define CPUJ_SUBI(rd, imm) CPUJ_INS(OP_SUBI, rd, 0, (imm) & 0xFFFFF)
#define CPUJ_ANDI(rd, imm) CPUJ_INS(OP_ANDI, rd, 0, (imm) & 0xFFFFF)
#define CPUJ_ORI(rd, imm)  CPUJ_INS(OP_ORI,  rd, 0, (imm) & 0xFFFFF)
#define CPUJ_XORI(rd, imm) CPUJ_INS(OP_XORI, rd, 0, (imm) & 0xFFFFF)
#define CPUJ_CMPI(rd, imm) CPUJ_INS(OP_CMPI, rd, 0, (imm) & 0xFFFFF)

#define CPUJ_NOT(rd)  CPUJ_INS(OP_NOT, rd, 0, 0)
#define CPUJ_SHL(rd, rs)    CPUJ_INS(OP_SHL,  rd, rs, 0)
#define CPUJ_SHR(rd, rs)    CPUJ_INS(OP_SHR,  rd, rs, 0)
#define CPUJ_SHLI(rd, n)    CPUJ_INS(OP_SHLI, rd, 0, (uint32_t)(n) & 31)
#define CPUJ_SHRI(rd, n)    CPUJ_INS(OP_SHRI, rd, 0, (uint32_t)(n) & 31)

/* LD/ST with a signed 20-bit displacement in bytes. */
#define CPUJ_LD(rd, rs, off)   CPUJ_INS(OP_LD,  rd, rs, (uint32_t)(off) & 0xFFFFF)
#define CPUJ_ST(rs, off, rd)   CPUJ_INS(OP_ST,  rd, rs, (uint32_t)(off) & 0xFFFFF)
#define CPUJ_LDB(rd, rs, off)  CPUJ_INS(OP_LDB, rd, rs, (uint32_t)(off) & 0xFFFFF)
#define CPUJ_STB(rs, off, rd)  CPUJ_INS(OP_STB, rd, rs, (uint32_t)(off) & 0xFFFFF)

/* Branches: rel is a signed 20-bit byte displacement from the next PC. */
#define CPUJ_BR(op, rel)  CPUJ_INS(op, 0, 0, (uint32_t)(rel) & 0xFFFFF)
#define CPUJ_JMP(rel)     CPUJ_BR(OP_JMP, rel)
#define CPUJ_JEQ(rel)     CPUJ_BR(OP_JEQ, rel)
#define CPUJ_JNE(rel)     CPUJ_BR(OP_JNE, rel)
#define CPUJ_JGT(rel)     CPUJ_BR(OP_JGT, rel)
#define CPUJ_JGE(rel)     CPUJ_BR(OP_JGE, rel)
#define CPUJ_JLT(rel)     CPUJ_BR(OP_JLT, rel)
#define CPUJ_JLE(rel)     CPUJ_BR(OP_JLE, rel)
#define CPUJ_CALL(rel)    CPUJ_BR(OP_CALL, rel)

#define CPUJ_JMPR(rs)    CPUJ_INS(OP_JMPR, 0, rs, 0)
#define CPUJ_CALLR(rs)   CPUJ_INS(OP_CALLR, 0, rs, 0)

#define CPUJ_PUSH(rd)  CPUJ_INS(OP_PUSH, rd, 0, 0)
#define CPUJ_POP(rd)   CPUJ_INS(OP_POP,  rd, 0, 0)
#define CPUJ_RET()     CPUJ_INS(OP_RET,  0, 0, 0)
#define CPUJ_TRAP(c)   CPUJ_INS(OP_TRAP, (uint32_t)(c) & 0x7, 0, 0)
#define CPUJ_HALT()    CPUJ_INS(OP_HALT, 0, 0, 0)
#define CPUJ_NOP()     CPUJ_INS(OP_NOP,  0, 0, 0)

/* ── CPU interface ────────────────────────────────────────────────── */

/* Allocate the 4 GiB of RAM and reset the machine. Returns false if
 * the allocation failed (don't call cpuj_free in that case). */
bool     cpuj_init(cpuj_t *cpu);
void     cpuj_free(cpuj_t *cpu);
void     cpuj_reset(cpuj_t *cpu);
void     cpuj_tick(cpuj_t *cpu);
void     cpuj_execute(cpuj_t *cpu, uint32_t instr);
void     cpuj_trap(cpuj_t *cpu, int code);

void     cpuj_mem_write(cpuj_t *cpu, uint32_t addr, uint8_t val);
uint8_t  cpuj_mem_read(cpuj_t *cpu, uint32_t addr);
void     cpuj_mem_write32(cpuj_t *cpu, uint32_t addr, uint32_t val);
uint32_t cpuj_mem_read32(cpuj_t *cpu, uint32_t addr);
void     cpuj_push(cpuj_t *cpu, uint32_t val);
uint32_t cpuj_pop(cpuj_t *cpu);
uint32_t cpuj_alu(cpuj_t *cpu, int op, uint32_t va, uint32_t vb);

int cpuj_disassemble(uint32_t instr, char *buf, int bufsize);

#endif /* CPUJ_H */