#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "cpuj.h"

/* ── Init / reset ─────────────────────────────────────────────────── */

void cpuj_init(cpuj_t *cpu) {
    memset(cpu->ram, 0, CPUJ_RAM_SIZE);
    cpuj_reset(cpu);
}

void cpuj_reset(cpuj_t *cpu) {
    for (int i = 0; i < CPUJ_REG_COUNT; i++)
        cpu->r[i] = 0;
    cpu->sp  = CPUJ_STACK_TOP;
    cpu->pc  = CPUJ_PROGRAM_START;
    cpu->flags = 0;
    cpu->halted = false;
}

/* ── Memory ───────────────────────────────────────────────────────── */

void cpuj_mem_write(cpuj_t *cpu, uint16_t addr, uint8_t val) {
    cpu->ram[addr] = val;
}

uint8_t cpuj_mem_read(cpuj_t *cpu, uint16_t addr) {
    return cpu->ram[addr];
}

void cpuj_push16(cpuj_t *cpu, uint16_t val) {
    if (cpu->sp < CPUJ_STACK_BOTTOM + 1) return;
    cpuj_mem_write(cpu, cpu->sp--, (uint8_t)(val >> 8));
    cpuj_mem_write(cpu, cpu->sp--, (uint8_t)val);
    /* SP now points at the low byte below the pushed word */
}

uint16_t cpuj_pop16(cpuj_t *cpu) {
    if (cpu->sp + 2 > CPUJ_STACK_TOP) return 0;
    uint8_t lo = cpuj_mem_read(cpu, ++cpu->sp);
    uint8_t hi = cpuj_mem_read(cpu, ++cpu->sp);
    return ((uint16_t)hi << 8) | lo;
}

/* ── ALU ───────────────────────────────────────────────────────────── */

uint16_t cpuj_alu(cpuj_t *cpu, int op, uint16_t a, uint16_t b) {
    uint16_t res = 0;
    bool carry = false, overflow = false;

    switch (op) {
        case OP_ADD: {
            uint32_t sum = (uint32_t)a + b;
            res    = (uint16_t)sum;
            carry  = sum > 0xFFFF;
            overflow = ((a & 0x8000) == (b & 0x8000)) &&
                       ((res & 0x8000) != (a & 0x8000));
            break;
        }
        case OP_SUB:
        case OP_CMP: {
            carry = a < b;
            res   = (uint16_t)(a - b);
            overflow = ((a & 0x8000) != (b & 0x8000)) &&
                       ((res & 0x8000) != (a & 0x8000));
            break;
        }
        case OP_AND:  res = a & b; break;
        case OP_OR:   res = a | b; break;
        case OP_XOR:  res = a ^ b; break;
        case OP_NOT:  res = (uint16_t)~a; break;
        case OP_SHL:  res = (uint16_t)(a << (b & 15)); break;
        case OP_SHR:  res = (uint16_t)(a >> (b & 15)); break;
        default:      return a;
    }

    uint8_t f = 0;
    if (res == 0)       f |= CPUJ_FLAG_Z;
    if (carry)          f |= CPUJ_FLAG_C;
    if (res & 0x8000)   f |= CPUJ_FLAG_N;
    if (overflow)       f |= CPUJ_FLAG_V;
    cpu->flags = f;
    return res;
}

/* ── Fetch ─────────────────────────────────────────────────────────── */

/* Word is read big-endian (high byte first). Returns a uint32 holding
 * word1 in bits 16-31; if the instruction is long, word2 in bits 0-15.
 * Advance PC by the instruction length (2 or 4 bytes). */
static uint32_t cpuj_fetch(cpuj_t *cpu) {
    uint16_t addr = cpu->pc;
    uint16_t w1hi = cpuj_mem_read(cpu, addr);
    uint16_t w1lo = cpuj_mem_read(cpu, addr + 1);
    uint16_t word1 = (uint16_t)((w1hi << 8) | w1lo);

    uint32_t instr = (uint32_t)word1 << 16;

    int imm6 = word1 & 0x3F;
    if (imm6 == IMM_LONG) {
        uint16_t w2hi = cpuj_mem_read(cpu, addr + 2);
        uint16_t w2lo = cpuj_mem_read(cpu, addr + 3);
        uint16_t word2 = (uint16_t)((w2hi << 8) | w2lo);
        instr |= word2;
        cpu->pc += 4;
    } else {
        cpu->pc += 2;
    }
    return instr;
}

/* ── Trap ──────────────────────────────────────────────────────────── */

void cpuj_trap(cpuj_t *cpu, int code) {
    switch (code) {
        case TRAP_PRINT_REG:
            printf("%u", cpu->r[0]);
            fflush(stdout);
            break;
        case TRAP_PRINT_CHAR:
            putchar(cpu->r[0] & 0xFF);
            fflush(stdout);
            break;
        case TRAP_READ_CHAR: {
            int c = getchar();
            cpu->r[0] = (c == EOF) ? 0 : (uint16_t)(c & 0xFF);
            break;
        }
        case TRAP_PRINT_STR: {
            uint32_t p = cpu->r[0];
            while (p < CPUJ_RAM_SIZE) {
                uint8_t ch = cpuj_mem_read(cpu, (uint16_t)p);
                if (ch == 0) break;
                putchar(ch);
                p++;
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

/* ── Branch conditions ────────────────────────────────────────────── */

static bool branch_taken(cpuj_t *cpu, int cond) {
    uint8_t f = cpu->flags;
    switch (cond) {
        case JMP_ALWAYS: return true;
        case JMP_EQ: return (f & CPUJ_FLAG_Z) != 0;
        case JMP_NE: return (f & CPUJ_FLAG_Z) == 0;
        case JMP_GT: return !(f & (CPUJ_FLAG_Z | CPUJ_FLAG_N));
        case JMP_GE: return ((f & CPUJ_FLAG_N) != 0) == ((f & CPUJ_FLAG_V) != 0);
        case JMP_LT: return ((f & CPUJ_FLAG_N) != 0) != ((f & CPUJ_FLAG_V) != 0);
        case JMP_LE: return (f & CPUJ_FLAG_Z) || (((f & CPUJ_FLAG_N) != 0) != ((f & CPUJ_FLAG_V) != 0));
        default: return false;
    }
}

/* ── Execute ───────────────────────────────────────────────────────── */

void cpuj_execute(cpuj_t *cpu, uint32_t instr) {
    int op = CPUJ_OP(instr);
    int rd = CPUJ_RD(instr);
    int rs = CPUJ_RS(instr);
    int imm = CPUJ_IMM(instr);
    int16_t reldisp = (int16_t)((imm < 32) ? imm : imm - 64); /* signed 6-bit */
    bool islong = (imm == IMM_LONG);
    uint16_t wide = CPUJ_WIDE(instr);

    switch (op) {
        case OP_MOV:
            if (islong)
                cpu->r[rd] = wide;
            else
                cpu->r[rd] = cpu->r[rs];
            break;

        case OP_ADD: case OP_SUB: case OP_AND:
        case OP_OR:  case OP_XOR: case OP_CMP: {
            uint16_t src = islong ? wide : cpu->r[rs];
            uint16_t res = cpuj_alu(cpu, op, cpu->r[rd], src);
            if (op != OP_CMP)
                cpu->r[rd] = res;
            break;
        }

        case OP_NOT:
            cpu->r[rd] = cpuj_alu(cpu, OP_NOT, cpu->r[rd], 0);
            break;

        case OP_SHL:
        case OP_SHR:
            cpu->r[rd] = cpuj_alu(cpu, op, cpu->r[rd], (uint16_t)(islong ? wide & 15 : imm & 15));
            break;

        case OP_LD: {
            uint16_t addr;
            if (islong)
                addr = wide;
            else
                addr = (uint16_t)(cpu->r[rs] + reldisp);
            cpu->r[rd] = cpuj_mem_read(cpu, addr);
            break;
        }
        case OP_ST: {
            uint16_t addr;
            if (islong)
                addr = wide;
            else
                addr = (uint16_t)(cpu->r[rs] + reldisp);
            cpuj_mem_write(cpu, addr, (uint8_t)(cpu->r[rd] & 0xFF));
            break;
        }

        case OP_JMP: {
            if (islong) {
                if (branch_taken(cpu, rd))
                    cpu->pc = wide;
            } else {
                int cond = rs;
                if (branch_taken(cpu, cond))
                    cpu->pc = (uint16_t)(cpu->pc + reldisp * 2);
            }
            break;
        }

        case OP_MISC:
            if (imm == IMM_LONG) {           /* CALL abs16 */
                cpuj_push16(cpu, cpu->pc);
                cpu->pc = wide;
            } else {
                switch (imm) {
                    case MISC_PUSH:
                        cpuj_push16(cpu, cpu->r[rd]);
                        break;
                    case MISC_POP:
                        cpu->r[rd] = cpuj_pop16(cpu);
                        break;
                    case MISC_CALL:          /* call Rs (indirect) */
                        cpuj_push16(cpu, cpu->pc);
                        cpu->pc = cpu->r[rs];
                        break;
                    case MISC_RET:
                        cpu->pc = cpuj_pop16(cpu);
                        break;
                    case MISC_TRAP:
                        cpuj_trap(cpu, rs);  /* trap code in rs field */
                        break;
                    case MISC_HALT:
                        cpu->halted = true;
                        break;
                    case MISC_NOP:
                        break;
                    default:
                        break;
                }
            }
            break;

        default:
            break; /* reserved */
    }
}

void cpuj_tick(cpuj_t *cpu) {
    if (cpu->halted) return;
    uint32_t instr = cpuj_fetch(cpu);
    cpuj_execute(cpu, instr);
}

/* ── Disassembler ──────────────────────────────────────────────────── */

static const char *reg_name(int r) {
    static const char *n[8] = { "R0","R1","R2","R3","R4","R5","R6","R7" };
    return (r >= 0 && r < 8) ? n[r] : "??";
}

static int emit(char *buf, int bufsize, int n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + n, (size_t)(bufsize - n), fmt, ap);
    va_end(ap);
    if (w < 0 || w >= bufsize - n) return n;
    return n + w;
}

static const char *cond_name(int c) {
    static const char *n[8] = { "JMP","JEQ","JNE","JGT","JGE","JLT","JLE","?" };
    return (c >= 0 && c < 8) ? n[c] : "?";
}

int cpuj_disassemble(uint32_t instr, char *buf, int bufsize) {
    int op = CPUJ_OP(instr), rd = CPUJ_RD(instr), rs = CPUJ_RS(instr);
    int imm = CPUJ_IMM(instr);
    uint16_t wide = CPUJ_WIDE(instr);
    bool islong = (imm == IMM_LONG);
    int n = 0;

    switch (op) {
        case OP_MOV:
            if (islong)
                n = emit(buf, bufsize, n, "MOVI %s, #%u", reg_name(rd), wide);
            else
                n = emit(buf, bufsize, n, "MOV %s, %s", reg_name(rd), reg_name(rs));
            break;
        case OP_ADD: case OP_SUB: case OP_AND: case OP_OR: case OP_XOR: case OP_CMP: {
            const char *mn = op==OP_ADD?"ADD":op==OP_SUB?"SUB":op==OP_AND?"AND":op==OP_OR?"OR":op==OP_XOR?"XOR":"CMP";
            if (islong)
                n = emit(buf, bufsize, n, "%s %s, #%u", mn, reg_name(rd), wide);
            else
                n = emit(buf, bufsize, n, "%s %s, %s", mn, reg_name(rd), reg_name(rs));
            break;
        }
        case OP_NOT:
            n = emit(buf, bufsize, n, "NOT %s", reg_name(rd));
            break;
        case OP_SHL:
        case OP_SHR:
            n = emit(buf, bufsize, n, "%s %s, #%u", op==OP_SHL?"SHL":"SHR", reg_name(rd),
                     (int)(islong ? wide & 15 : imm & 15));
            break;
        case OP_LD:
        case OP_ST: {
            const char *mn = op==OP_LD?"LD":"ST";
            if (islong) {
                if (op==OP_LD)
                    n = emit(buf, bufsize, n, "LD %s, [0x%04X]", reg_name(rd), wide);
                else
                    n = emit(buf, bufsize, n, "ST [0x%04X], %s", wide, reg_name(rd));
            } else {
                int off = (imm<32) ? imm : imm-64;
                n = emit(buf, bufsize, n, "%s %s, [%s%+d]", mn, reg_name(rd), reg_name(rs), off);
            }
            break;
        }
        case OP_JMP:
            if (islong)
                n = emit(buf, bufsize, n, "%s 0x%04X", cond_name(rd), wide);
            else
                n = emit(buf, bufsize, n, "%s %+d", cond_name(rs),
                         (int)((imm<32)?imm:imm-64));
            break;
        case OP_MISC:
            if (islong)
                n = emit(buf, bufsize, n, "CALL 0x%04X", wide);
            else
                switch (imm) {
                    case MISC_PUSH: n = emit(buf, bufsize, n, "PUSH %s", reg_name(rd)); break;
                    case MISC_POP:  n = emit(buf, bufsize, n, "POP %s", reg_name(rd)); break;
                    case MISC_CALL: n = emit(buf, bufsize, n, "CALL %s", reg_name(rs)); break;
                    case MISC_RET:  n = emit(buf, bufsize, n, "RET"); break;
                    case MISC_TRAP: n = emit(buf, bufsize, n, "TRAP %d", rs); break;
                    case MISC_HALT: n = emit(buf, bufsize, n, "HALT"); break;
                    case MISC_NOP:  n = emit(buf, bufsize, n, "NOP"); break;
                    default: n = emit(buf, bufsize, n, "??"); break;
                }
            break;
        default:
            n = emit(buf, bufsize, n, "??");
            break;
    }
    return n;
}