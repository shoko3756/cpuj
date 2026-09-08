#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "cpuj1.h"

/* ── Init / reset ─────────────────────────────────────────────────── */

void cpuj1_init(cpuj1_t *cpu) {
    for (int i = 0; i < CPUJ1_RAM_SIZE; i++)
        cpu->ram[i] = 0;
    cpuj1_reset(cpu);
}

void cpuj1_reset(cpuj1_t *cpu) {
    for (int i = 0; i < CPUJ1_REG_COUNT; i++)
        cpu->r[i] = 0;
    cpu->pc  = CPUJ1_PROGRAM_START;
    cpu->sp  = CPUJ1_STACK_TOP;
    cpu->flags = 0;
    cpu->halted = false;
    /* RAM is preserved across reset, like a soft reset */
}

/* ── Memory ───────────────────────────────────────────────────────── */

void cpuj1_mem_write(cpuj1_t *cpu, uint16_t addr, uint8_t val) {
    if (addr >= CPUJ1_RAM_SIZE) return;
    cpu->ram[addr] = val;
}

uint8_t cpuj1_mem_read(cpuj1_t *cpu, uint16_t addr) {
    if (addr >= CPUJ1_RAM_SIZE) return 0;
    return cpu->ram[addr];
}

void cpuj1_push(cpuj1_t *cpu, uint8_t val) {
    if (cpu->sp == CPUJ1_STACK_BOTTOM - 1) return; /* overflow */
    cpu->ram[cpu->sp--] = val;
}

uint8_t cpuj1_pop(cpuj1_t *cpu) {
    if (cpu->sp == CPUJ1_STACK_TOP) return 0; /* underflow */
    return cpu->ram[++cpu->sp];
}

/* ── Decode fields ─────────────────────────────────────────────────── */

static inline int   op_of(uint16_t i) { return (i >> 12) & 0xF; }
static inline int   rd_of(uint16_t i) { return (i >> 10) & 0x3; }
static inline int   rs_of(uint16_t i) { return (i >>  8) & 0x3; }
static inline uint8_t op8 (uint16_t i) { return (uint8_t)i; }

/* ── ALU ───────────────────────────────────────────────────────────── */

uint8_t cpuj1_alu(cpuj1_t *cpu, int op, uint8_t a, uint8_t b) {
    uint8_t  res = 0;
    bool     carry = false;

    switch (op) {
        case OP_ADD: {
            uint16_t sum = (uint16_t)a + b;
            res   = (uint8_t)sum;
            carry = sum > 0xFF;
            break;
        }
        case OP_SUB:
        case OP_CMP:
            carry = a < b;
            res   = (uint8_t)(a - b);
            break;
        case OP_AND:  res = a & b; break;
        case OP_OR:   res = a | b; break;
        case OP_XOR:  res = a ^ b; break;
        case OP_NOT:  res = (uint8_t)~a; break;
        case OP_SHL:  res = (uint8_t)(a << (b & 15)); break;
        case OP_SHR:  res = (uint8_t)(a >> (b & 15)); break;
        default:      return a;
    }

    uint8_t f = 0;
    if (res == 0)         f |= CPUJ1_FLAG_Z;
    if (carry)            f |= CPUJ1_FLAG_C;
    if (res & 0x80)       f |= CPUJ1_FLAG_N;
    cpu->flags = f;
    return res;
}

/* ── Fetch ─────────────────────────────────────────────────────────── */

/* Instruction words live in RAM big-endian: high byte at the even
 * address, low byte at the following odd address. PC advances by 2. */
uint16_t cpuj1_fetch(cpuj1_t *cpu) {
    uint16_t addr = cpu->pc & 0xFF; /* wrap within VM address space */
    uint16_t hi   = cpuj1_mem_read(cpu, addr);
    uint16_t lo   = cpuj1_mem_read(cpu, addr + 1);
    cpu->pc = (uint16_t)(cpu->pc + 2);
    return (hi << 8) | lo;
}

/* ── Traps (I/O) ───────────────────────────────────────────────────── */

void cpuj1_trap(cpuj1_t *cpu, int code) {
    switch (code) {
        case TRAP_PRINT_REG:
            printf("%u", cpu->r[0]);
            fflush(stdout);
            break;
        case TRAP_PRINT_CHAR:
            putchar(cpu->r[0]);
            fflush(stdout);
            break;
        case TRAP_READ_CHAR: {
            int c = getchar();
            cpu->r[0] = (c == EOF) ? 0 : (uint8_t)c;
            break;
        }
        case TRAP_PRINT_STR: {
            uint16_t p = cpu->r[0];
            while (p < CPUJ1_RAM_SIZE) {
                uint8_t ch = cpuj1_mem_read(cpu, p++);
                if (ch == 0) break;
                putchar(ch);
            }
            fflush(stdout);
            break;
        }
        case TRAP_HALT:
            cpu->halted = true;
            break;
        default:
            break;
    }
}

static bool branch_taken(cpuj1_t *cpu, int cond) {
    switch (cond) {
        case JMP_ALWAYS: return true;
        case JMP_EQ:     return (cpu->flags & CPUJ1_FLAG_Z) != 0;
        case JMP_NE:     return (cpu->flags & CPUJ1_FLAG_Z) == 0;
        case JMP_GT:     return (cpu->flags & (CPUJ1_FLAG_Z | CPUJ1_FLAG_N)) == 0;
        default:         return false;
    }
}

/* ── Tick: fetch + execute one instruction ────────────────────────── */

void cpuj1_tick(cpuj1_t *cpu) {
    if (cpu->halted) return;
    uint16_t instr = cpuj1_fetch(cpu);
    cpuj1_execute(cpu, instr);
}

/* ── Execute ───────────────────────────────────────────────────────── */

void cpuj1_execute(cpuj1_t *cpu, uint16_t instr) {
    int op = op_of(instr);
    int rd = rd_of(instr);
    int rs = rs_of(instr);
    uint8_t v = op8(instr);

    switch (op) {
        case OP_MOV:
            cpu->r[rd] = cpu->r[rs];
            break;

        case OP_MOVI:
            cpu->r[rd] = v;
            break;

        case OP_ADD: case OP_SUB: case OP_AND:
        case OP_OR:  case OP_XOR: case OP_CMP: {
            uint8_t src = (rs == CPUJ1_RS_IMM) ? v : cpu->r[rs];
            uint8_t res = cpuj1_alu(cpu, op, cpu->r[rd], src);
            if (op != OP_CMP)
                cpu->r[rd] = res;
            break;
        }

        case OP_NOT:
            cpu->r[rd] = cpuj1_alu(cpu, OP_NOT, cpu->r[rd], 0);
            break;

        case OP_SHL:
        case OP_SHR:
            cpu->r[rd] = cpuj1_alu(cpu, op, cpu->r[rd], v & 0x0F);
            break;

        case OP_LD:
            if (v == 0x01)
                cpu->r[rd] = cpuj1_mem_read(cpu, cpu->r[rs]);       /* [Ri] */
            else
                cpu->r[rd] = cpuj1_mem_read(cpu, v);                /* [addr] */
            break;

        case OP_ST:
            if (v == 0x01)
                cpuj1_mem_write(cpu, cpu->r[rs], cpu->r[rd]);       /* [Ri] */
            else
                cpuj1_mem_write(cpu, v, cpu->r[rd]);                /* [addr] */
            break;

        case OP_JMP:
            if (branch_taken(cpu, rs))
                cpu->pc = (uint16_t)v;
            break;

        case OP_CALL:
            cpuj1_push(cpu, (uint8_t)cpu->pc);
            cpu->pc = (uint16_t)v;
            break;

        case OP_MISC:
            if (v == MISC_PUSH) {
                cpuj1_push(cpu, cpu->r[rs]);
            } else if (v == MISC_POP) {
                cpu->r[rd] = cpuj1_pop(cpu);
            } else if (v == MISC_RET) {
                cpu->pc = cpuj1_pop(cpu);
            } else if ((v & 0xF0) == MISC_TRAP) {
                cpuj1_trap(cpu, v & 0x0F);
            } else if (v == MISC_HALT) {
                cpu->halted = true;
            } else if (v == MISC_NOP) {
                /* nothing */
            }
            break;

        default:
            break; /* reserved */
    }
}

/* ── Disassembler ──────────────────────────────────────────────────── */

static const char *reg_name(int r) {
    static const char *n[4] = { "R0", "R1", "R2", "R3" };
    return (r >= 0 && r < 4) ? n[r] : "??";
}

static int emit(char *buf, int bufsize, int n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, (size_t)(bufsize - n), fmt, ap);
    va_end(ap);
    if (w < 0 || w >= bufsize - n) return n;
    return n + w;
}

static int dis_alu(const char *mn, int rd, int rs, uint8_t v, char *buf, int size) {
    int n = 0;
    if (rs == CPUJ1_RS_IMM)
        n = emit(buf, size, n, "%s %s, #%u", mn, reg_name(rd), v);
    else
        n = emit(buf, size, n, "%s %s, %s", mn, reg_name(rd), reg_name(rs));
    return n;
}

int cpuj1_disassemble(uint16_t instr, char *buf, int bufsize) {
    int op = op_of(instr), rd = rd_of(instr), rs = rs_of(instr);
    uint8_t v = op8(instr);
    int n = 0;
    static const char *jmp_mn[4] = { "JMP", "JEQ", "JNE", "JGT" };
    static const char *trap_mn[16] = {
        "PRINT_REG", "PRINT_CHAR", "READ_CHAR", "PRINT_STR",
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "HALT"
    };

    switch (op) {
        case OP_MOV:
            n = emit(buf, bufsize, n, "MOV %s, %s", reg_name(rd), reg_name(rs));
            break;
        case OP_MOVI:
            n = emit(buf, bufsize, n, "MOVI %s, #%u", reg_name(rd), v);
            break;
        case OP_ADD:  n = dis_alu("ADD",  rd, rs, v, buf, bufsize); break;
        case OP_SUB:  n = dis_alu("SUB",  rd, rs, v, buf, bufsize); break;
        case OP_AND:  n = dis_alu("AND",  rd, rs, v, buf, bufsize); break;
        case OP_OR:   n = dis_alu("OR",   rd, rs, v, buf, bufsize); break;
        case OP_XOR:  n = dis_alu("XOR",  rd, rs, v, buf, bufsize); break;
        case OP_CMP:  n = dis_alu("CMP",  rd, rs, v, buf, bufsize); break;
        case OP_NOT:
            n = emit(buf, bufsize, n, "NOT %s", reg_name(rd));
            break;
        case OP_SHL:
            n = emit(buf, bufsize, n, "SHL %s, #%u", reg_name(rd), v & 0x0F);
            break;
        case OP_SHR:
            n = emit(buf, bufsize, n, "SHR %s, #%u", reg_name(rd), v & 0x0F);
            break;
        case OP_LD:
            if (v == 0x01)
                n = emit(buf, bufsize, n, "LD %s, [%s]", reg_name(rd), reg_name(rs));
            else
                n = emit(buf, bufsize, n, "LD %s, [0x%02X]", reg_name(rd), v);
            break;
        case OP_ST:
            if (v == 0x01)
                n = emit(buf, bufsize, n, "ST [%s], %s", reg_name(rs), reg_name(rd));
            else
                n = emit(buf, bufsize, n, "ST [0x%02X], %s", v, reg_name(rd));
            break;
        case OP_JMP:
            if (rs >= 0 && rs < 4)
                n = emit(buf, bufsize, n, "%s 0x%02X", jmp_mn[rs], v);
            else
                n = emit(buf, bufsize, n, "JMP 0x%02X", v);
            break;
        case OP_CALL:
            n = emit(buf, bufsize, n, "CALL 0x%02X", v);
            break;
        case OP_MISC:
            switch (v) {
                case MISC_PUSH:
                    n = emit(buf, bufsize, n, "PUSH %s", reg_name(rs));
                    break;
                case MISC_POP:
                    n = emit(buf, bufsize, n, "POP %s", reg_name(rd));
                    break;
                case MISC_RET:
                    n = emit(buf, bufsize, n, "RET");
                    break;
                case MISC_HALT:
                    n = emit(buf, bufsize, n, "HALT");
                    break;
                case MISC_NOP:
                    n = emit(buf, bufsize, n, "NOP");
                    break;
                default:
                    if ((v & 0xF0) == MISC_TRAP) {
                        const char *t = trap_mn[v & 0x0F];
                        if (t)
                            n = emit(buf, bufsize, n, "TRAP %s", t);
                        else
                            n = emit(buf, bufsize, n, "TRAP %d", v & 0x0F);
                    } else
                        n = emit(buf, bufsize, n, "??");
                    break;
            }
            break;
        default:
            n = emit(buf, bufsize, n, "??");
            break;
    }
    return n;
}