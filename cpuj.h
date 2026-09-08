#ifndef CPUJ_H
#define CPUJ_H

#include <stdint.h>
#include <stdbool.h>

/* ═══════════════════════════════════════════════════════════════════
 *                      cpuj — 16-bit virtual CPU
 *
 *  8 general-purpose registers (R0-R7), each 16 bits.
 *  64 KB of addressable memory. Mixed-length instructions (see below).
 *  Flags: Z, C, N, V.
 *
 *  Memory map:
 *    0x0000 .. 0xEFFF   program + data
 *    0xFFF0 .. 0xFFFF   stack (grows down from 0xFFFF)
 *
 *  SP starts at 0xFFFF; push pre-decrements, pop post-increments.
 *  Programs load at 0x0000. Instructions are stored big-endian.
 * ══════════════════════════════════════════════════════════════════ */

#define CPUJ_RAM_SIZE       0x10000
#define CPUJ_REG_COUNT      8
#define CPUJ_STACK_TOP      0xFFFF
#define CPUJ_STACK_BOTTOM   0xFFF0
#define CPUJ_PROGRAM_START  0x0000

/* ── Register state ──────────────────────────────────────────────── */

typedef struct {
    uint16_t r[CPUJ_REG_COUNT]; /* R0-R7 */
    uint16_t sp;
    uint16_t pc;
    uint8_t  flags;             /* Z C N V */
    uint8_t  ram[CPUJ_RAM_SIZE];
    bool     halted;
} cpuj_t;

#define CPUJ_FLAG_Z (1 << 0)
#define CPUJ_FLAG_C (1 << 1)
#define CPUJ_FLAG_N (1 << 2)
#define CPUJ_FLAG_V (1 << 3)

/* ═══════════════════════════════════════════════════════════════════
 *   Instruction encoding
 *
 *   SHORT (16-bit):  [ op:4 ][ rd:3 ][ rs:3 ][ imm:6 ]
 *                     ^high^                     ^low^
 *
 *   LONG (32-bit):   a short word whose imm field is the sentinel
 *   IMM_LONG (0x3F). The following 16-bit word holds imm16/addr16:
 *
 *     word1 = [ op:4 ][ rd:3 ][ rs:3 ][ 111111 ]
 *     word2 = [ imm16 / addr16 ]
 *
 *   This keeps a single opcode space; any opcode may use the long form.
 *   Register-register operations never set IMM_LONG, so there is no
 *   ambiguity between "register source" and "wide immediate".
 *
 *   ┌───────┬────────────────────────────────────────────────────────┐
 *   │ OP    │ SHORT (register)               LONG (imm16 / addr16)  │
 *   ├───────┼────────────────────────────────────────────────────────┤
 *   │ 0 MOV │ MOV Rd, Rs                     MOV Rd, #imm16          │
 *   │ 1     │ (reserved; use MOV long)       (move handled by op 0)  │
 *   │ 2 ADD │ ADD Rd, Rs                     ADD Rd, #imm16          │
 *   │ 3 SUB │ SUB Rd, Rs                     SUB Rd, #imm16          │
 *   │ 4 AND │ AND Rd, Rs                     AND Rd, #imm16          │
 *   │ 5 OR  │ OR  Rd, Rs                     OR  Rd, #imm16          │
 *   │ 6 XOR │ XOR Rd, Rs                     XOR Rd, #imm16          │
 *   │ 7 NOT │ NOT Rd                         —                       │
 *   │ 8 SHL │ SHL Rd, #n  (n in imm 0..7)    SHL Rd, #n16            │
 *   │ 9 SHR │ SHR Rd, #n  (n in imm 0..7)    SHR Rd, #n16            │
 *   │ A CMP │ CMP Rd, Rs                     CMP Rd, #imm16          │
 *   │ B LD  │ LD Rd, [Rs+off] (off in imm)   LD  Rd, [addr16]        │
 *   │ C ST  │ ST [Rs+off], Rd (off in imm)   ST  [addr16], Rd        │
 *   │ D JMP │ condition-coded branch         JMP/CALL addr16 (abs)   │
 *   │ E MISC│ PUSH/POP/RET/TRAP/HALT/NOP     CALL addr16             │
 *   │ F     │ (reserved)                     —                       │
 *   └───────┴────────────────────────────────────────────────────────┘
 *
 *   MOVI uses opcode 0 in long form (MOV Rd, #imm16).
 *
 *   JMP (op D):
 *     SHORT, rs selects condition:
 *       rs=0 JMP [always], rs=1 JEQ(Z), rs=2 JNE(!Z), rs=3 JGT,
 *       rs=4 JGE(N==V), rs=5 JLT(N!=V), rs=6 JLE(Z||(N!=V))
 *     imm is a signed 6-bit PC-relative offset in WORDS (-32..+31).
 *     imm==-1 (0x3F) is the long-form marker and cannot be a rel
 *     target; use the long form instead.
 *
 *   MISC (op E), imm selects subtype:
 *      0 PUSH Rs      (register to push lives in rd field)
 *      1 POP  Rd
 *      2 CALL Rs      (register-indirect call; rd=0)
 *      3 RET
 *      4 TRAP code    (code = rs field, 0..7)
 *      5 HALT
 *      6 NOP
 *      long form (imm=0x3F): CALL addr16 (a return address is pushed)
 *
 *   LD/ST SHORT: address = Rs + signed(imm); any register (R0..R7)
 *   may be a base. LD/ST LONG: address = addr16 from second word.
 *
 *   Not encodable and rejected by the assembler: LD/ST offset -1
 *   (0x3F is the long-form marker) and LD/ST offsets outside -32..+31.
 * ══════════════════════════════════════════════════════════════════ */

typedef enum {
    OP_MOV  = 0x0,
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
    OP_MISC = 0xE
} cpuj_opcode_t;

#define IMM_LONG 0x3F   /* imm==0x3F marks a 32-bit long form */

/* Jump conditions (rs field of OP_JMP) */
typedef enum {
    JMP_ALWAYS = 0,
    JMP_EQ     = 1,
    JMP_NE     = 2,
    JMP_GT     = 3,
    JMP_GE     = 4,
    JMP_LT     = 5,
    JMP_LE     = 6
} cpuj_jump_t;

/* MISC subtypes (imm field of OP_MISC) */
typedef enum {
    MISC_PUSH = 0,
    MISC_POP  = 1,
    MISC_CALL = 2,   /* call Rs (register-indirect) */
    MISC_RET  = 3,
    MISC_TRAP = 4,
    MISC_HALT = 5,
    MISC_NOP  = 6
} cpuj_misc_t;

typedef enum {
    TRAP_PRINT_REG  = 0,
    TRAP_PRINT_CHAR = 1,
    TRAP_READ_CHAR  = 2,
    TRAP_PRINT_STR  = 3,
    TRAP_HALT       = 6   /* fits in rs field (0..7) */
} cpuj_trap_t;

/* An instruction is represented as uint32_t = (word1 << 16) | word2.
 * Length is 32 bits iff the imm field of word1 == IMM_LONG. */
#define CPUJ_INSTR_LONG  0x3F00  /* bit pattern in the imm field */

/* Field accessors */
#define CPUJ_OP(i)   (((i) >> 28) & 0xF)   /* from word1 */
#define CPUJ_RD(i)   (((i) >> 25) & 0x7)
#define CPUJ_RS(i)   (((i) >> 22) & 0x7)
#define CPUJ_IMM(i)  (((i) >> 16) & 0x3F)
#define CPUJ_WIDE(i) ((i) & 0xFFFF)

static inline bool cpuj_is_long(uint32_t i) {
    return CPUJ_IMM(i) == IMM_LONG;
}

/* ── Constructors (short forms) ─────────────────────────────────── */

#define CPUJ_SHORT(op, rd, rs, imm) \
    ((uint32_t)(((uint32_t)(op) << 28) | \
                ((uint32_t)(rd) << 25) | \
                ((uint32_t)(rs) << 22) | \
                (((uint32_t)(imm) & 0x3F) << 16)))

#define CPUJ_LONG(op, rd, imm16) \
    ((uint32_t)(((uint32_t)(op) << 28) | \
                ((uint32_t)(rd) << 25) | \
                ((uint32_t)IMM_LONG << 16) | \
                ((uint32_t)(imm16) & 0xFFFF)))

#define CPUJ_MOV(rd, rs)    CPUJ_SHORT(OP_MOV, rd, rs, 0)
#define CPUJ_ADD(rd, rs)    CPUJ_SHORT(OP_ADD, rd, rs, 0)
#define CPUJ_SUB(rd, rs)    CPUJ_SHORT(OP_SUB, rd, rs, 0)
#define CPUJ_AND(rd, rs)    CPUJ_SHORT(OP_AND, rd, rs, 0)
#define CPUJ_OR(rd, rs)     CPUJ_SHORT(OP_OR,  rd, rs, 0)
#define CPUJ_XOR(rd, rs)    CPUJ_SHORT(OP_XOR, rd, rs, 0)
#define CPUJ_NOT(rd)        CPUJ_SHORT(OP_NOT, rd, 0, 0)
#define CPUJ_CMP(rd, rs)    CPUJ_SHORT(OP_CMP, rd, rs, 0)
#define CPUJ_SHL(rd, n)     CPUJ_SHORT(OP_SHL, rd, 0, (n) & 0x3F)
#define CPUJ_SHR(rd, n)     CPUJ_SHORT(OP_SHR, rd, 0, (n) & 0x3F)

/* LD/ST: [Rs + off]; off is a signed 6-bit offset (-32..+31). */
#define CPUJ_LD_OFF(rd, rs, off) CPUJ_SHORT(OP_LD, rd, rs, (off) & 0x3F)
#define CPUJ_ST_OFF(rs, off, rd) CPUJ_SHORT(OP_ST, rd, rs, (off) & 0x3F)

/* LD/ST with a 16-bit absolute address (long form) */
#define CPUJ_LD_ABS16(rd, addr)  CPUJ_LONG(OP_LD, rd, addr)
#define CPUJ_ST_ABS16(addr, rd)  CPUJ_LONG(OP_ST, rd, addr)

/* MOV / ALU with a 16-bit immediate (long form) */
#define CPUJ_MOVI(rd, imm16)  CPUJ_LONG(OP_MOV, rd, imm16)
#define CPUJ_ADDI(rd, imm16)  CPUJ_LONG(OP_ADD, rd, imm16)
#define CPUJ_SUBI(rd, imm16)  CPUJ_LONG(OP_SUB, rd, imm16)
#define CPUJ_ANDI(rd, imm16)  CPUJ_LONG(OP_AND, rd, imm16)
#define CPUJ_ORI(rd, imm16)   CPUJ_LONG(OP_OR,  rd, imm16)
#define CPUJ_XORI(rd, imm16)  CPUJ_LONG(OP_XOR, rd, imm16)
#define CPUJ_CMPI(rd, imm16)  CPUJ_LONG(OP_CMP, rd, imm16)
#define CPUJ_SHLI(rd, n)      CPUJ_LONG(OP_SHL, rd, n)
#define CPUJ_SHRI(rd, n)      CPUJ_LONG(OP_SHR, rd, n)

/* Jumps. `rel` is a signed 6-bit PC-relative word offset (SHORT, rs=cond)
 * or an absolute addr16 (LONG form, imm=0x3F). */
#define CPUJ_JMP(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_ALWAYS, (rel) & 0x3F)
#define CPUJ_JEQ(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_EQ, (rel) & 0x3F)
#define CPUJ_JNE(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_NE, (rel) & 0x3F)
#define CPUJ_JGT(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_GT, (rel) & 0x3F)
#define CPUJ_JGE(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_GE, (rel) & 0x3F)
#define CPUJ_JLT(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_LT, (rel) & 0x3F)
#define CPUJ_JLE(rel)            CPUJ_SHORT(OP_JMP, 0, JMP_LE, (rel) & 0x3F)
/* Absolute jump / call (long form). rd holds the condition (0=always). */
#define CPUJ_JB(cond, addr)      CPUJ_LONG(OP_JMP, cond, addr)
#define CPUJ_JMP_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_ALWAYS, addr)
#define CPUJ_JEQ_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_EQ, addr)
#define CPUJ_JNE_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_NE, addr)
#define CPUJ_JGT_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_GT, addr)
#define CPUJ_JGE_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_GE, addr)
#define CPUJ_JLT_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_LT, addr)
#define CPUJ_JLE_ABS(addr)       CPUJ_LONG(OP_JMP, JMP_LE, addr)
#define CPUJ_CALL_ABS(addr)      CPUJ_LONG(OP_MISC, 0, addr)

/* MISC */
#define CPUJ_PUSH(rs)  CPUJ_SHORT(OP_MISC, rs, 0, MISC_PUSH)
#define CPUJ_POP(rd)   CPUJ_SHORT(OP_MISC, rd, 0, MISC_POP)
#define CPUJ_CALL_REG(r) CPUJ_SHORT(OP_MISC, 0, r, MISC_CALL)
#define CPUJ_RET()     CPUJ_SHORT(OP_MISC, 0, 0, MISC_RET)
#define CPUJ_TRAP(c)   CPUJ_SHORT(OP_MISC, 0, (c) & 0x7, MISC_TRAP)
#define CPUJ_HALT()    CPUJ_SHORT(OP_MISC, 0, 0, MISC_HALT)
#define CPUJ_NOP()     CPUJ_SHORT(OP_MISC, 0, 0, MISC_NOP)

/* ── CPU interface ────────────────────────────────────────────────── */

void     cpuj_init(cpuj_t *cpu);
void     cpuj_reset(cpuj_t *cpu);
void     cpuj_tick(cpuj_t *cpu);
void     cpuj_execute(cpuj_t *cpu, uint32_t instr);

void     cpuj_mem_write(cpuj_t *cpu, uint16_t addr, uint8_t val);
uint8_t  cpuj_mem_read(cpuj_t *cpu, uint16_t addr);
void     cpuj_push16(cpuj_t *cpu, uint16_t val);
uint16_t cpuj_pop16(cpuj_t *cpu);
uint16_t cpuj_alu(cpuj_t *cpu, int op, uint16_t va, uint16_t vb);
void     cpuj_trap(cpuj_t *cpu, int code);

int cpuj_disassemble(uint32_t instr, char *buf, int bufsize);

#endif /* CPUJ_H */