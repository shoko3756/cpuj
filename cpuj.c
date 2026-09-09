#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "cpuj.h"

/* ── Init / reset ─────────────────────────────────────────────────── */

bool cpuj_init(cpuj_t *cpu) {
    cpu->ram = (uint8_t *)calloc(1, CPUJ_RAM_SIZE);
    if (!cpu->ram) return false;
    cpuj_reset(cpu);
    return true;
}

void cpuj_free(cpuj_t *cpu) {
    free(cpu->ram);
    cpu->ram = NULL;
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

void cpuj_mem_write(cpuj_t *cpu, uint32_t addr, uint8_t val) {
    cpu->ram[addr] = val;
}

uint8_t cpuj_mem_read(cpuj_t *cpu, uint32_t addr) {
    return cpu->ram[addr];
}

/* Words are big-endian: msb byte at the lowest address. */
void cpuj_mem_write32(cpuj_t *cpu, uint32_t addr, uint32_t val) {
    cpu->ram[addr]     = (uint8_t)(val >> 24);
    cpu->ram[addr + 1] = (uint8_t)(val >> 16);
    cpu->ram[addr + 2] = (uint8_t)(val >> 8);
    cpu->ram[addr + 3] = (uint8_t)val;
}

uint32_t cpuj_mem_read32(cpuj_t *cpu, uint32_t addr) {
    return ((uint32_t)cpu->ram[addr]     << 24) |
           ((uint32_t)cpu->ram[addr + 1] << 16) |
           ((uint32_t)cpu->ram[addr + 2] << 8)  |
           ((uint32_t)cpu->ram[addr + 3]);
}

/* Stack grows down from 0xFFFFFFF0; push pre-decrements by 4,
 * pop post-increments by 4. */
void cpuj_push(cpuj_t *cpu, uint32_t val) {
    if (cpu->sp < 4) return;
    cpu->sp -= 4;
    cpuj_mem_write32(cpu, cpu->sp, val);
}

uint32_t cpuj_pop(cpuj_t *cpu) {
    if (cpu->sp >= CPUJ_STACK_TOP) return 0;
    uint32_t val = cpuj_mem_read32(cpu, cpu->sp);
    cpu->sp += 4;
    return val;
}

/* ── ALU ───────────────────────────────────────────────────────────── */

uint32_t cpuj_alu(cpuj_t *cpu, int op, uint32_t a, uint32_t b) {
    uint32_t res = 0;
    bool carry = false, overflow = false;

    switch (op) {
        case OP_ADD: {
            uint64_t sum = (uint64_t)a + b;
            res    = (uint32_t)sum;
            carry  = sum > 0xFFFFFFFFULL;
            overflow = ((a & 0x80000000) == (b & 0x80000000)) &&
                       ((res & 0x80000000) != (a & 0x80000000));
            break;
        }
        case OP_SUB:
        case OP_CMP: {
            carry = a < b;
            res   = a - b;
            overflow = ((a & 0x80000000) != (b & 0x80000000)) &&
                       ((res & 0x80000000) != (a & 0x80000000));
            break;
        }
        case OP_AND:  res = a & b; break;
        case OP_OR:   res = a | b; break;
        case OP_XOR:  res = a ^ b; break;
        case OP_NOT:  res = ~a; break;
        case OP_SHL:  res = a << (b & 31); break;
        case OP_SHR:  res = a >> (b & 31); break;
        default:      return a;
    }

    uint8_t f = 0;
    if (res == 0)            f |= CPUJ_FLAG_Z;
    if (carry)               f |= CPUJ_FLAG_C;
    if (res & 0x80000000)    f |= CPUJ_FLAG_N;
    if (overflow)            f |= CPUJ_FLAG_V;
    cpu->flags = f;
    return res;
}

/* ── Fetch ─────────────────────────────────────────────────────────── */

/* Fetch one fixed 32-bit instruction (4 bytes, big-endian) and advance
 * PC by 4. */
uint32_t cpuj_fetch(cpuj_t *cpu) {
    uint32_t addr = cpu->pc;
    uint32_t ins  = cpuj_mem_read32(cpu, addr);
    cpu->pc += 4;
    return ins;
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
            cpu->r[0] = (c == EOF) ? 0 : (uint32_t)(c & 0xFF);
            break;
        }
        case TRAP_PRINT_STR: {
            uint32_t p = cpu->r[0];
            for (;;) {
                uint8_t ch = cpuj_mem_read(cpu, p);
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

static void do_alu(cpuj_t *cpu, int op, int rd, uint32_t b) {
    uint32_t res = cpuj_alu(cpu, op, cpu->r[rd], b);
    if (op != OP_CMP)
        cpu->r[rd] = res;
}

void cpuj_execute(cpuj_t *cpu, uint32_t ins) {
    int op  = CPUJ_OP(ins);
    int rd  = CPUJ_RD(ins);
    int rs  = CPUJ_RS(ins);
    int32_t simm = CPUJ_SIMM(ins);

    switch (op) {
        case OP_MOV:
            cpu->r[rd] = cpu->r[rs];
            break;

        case OP_MOVI:
            cpu->r[rd] = CPUJ_IMM(ins);
            break;

        case OP_MOVU:
            cpu->r[rd] = (CPUJ_IMM(ins) & 0xFFF) << 20;
            break;

        case OP_ADD: do_alu(cpu, OP_ADD, rd, cpu->r[rs]); break;
        case OP_ADDI: do_alu(cpu, OP_ADD, rd, CPUJ_IMM(ins)); break;
        case OP_SUB: do_alu(cpu, OP_SUB, rd, cpu->r[rs]); break;
        case OP_SUBI: do_alu(cpu, OP_SUB, rd, CPUJ_IMM(ins)); break;
        case OP_AND: do_alu(cpu, OP_AND, rd, cpu->r[rs]); break;
        case OP_ANDI: do_alu(cpu, OP_AND, rd, CPUJ_IMM(ins)); break;
        case OP_OR: do_alu(cpu, OP_OR, rd, cpu->r[rs]); break;
        case OP_ORI: do_alu(cpu, OP_OR, rd, CPUJ_IMM(ins)); break;
        case OP_XOR: do_alu(cpu, OP_XOR, rd, cpu->r[rs]); break;
        case OP_XORI: do_alu(cpu, OP_XOR, rd, CPUJ_IMM(ins)); break;
        case OP_CMP: do_alu(cpu, OP_CMP, rd, cpu->r[rs]); break;
        case OP_CMPI: do_alu(cpu, OP_CMP, rd, CPUJ_IMM(ins)); break;

        case OP_NOT:
            do_alu(cpu, OP_NOT, rd, 0);
            break;

        case OP_SHL: case OP_SHLI:
            do_alu(cpu, OP_SHL, rd, (op == OP_SHLI) ? CPUJ_IMM(ins) : cpu->r[rs]);
            break;
        case OP_SHR: case OP_SHRI:
            do_alu(cpu, OP_SHR, rd, (op == OP_SHRI) ? CPUJ_IMM(ins) : cpu->r[rs]);
            break;

        case OP_LD:
            cpu->r[rd] = cpuj_mem_read32(cpu, cpu->r[rs] + simm);
            break;
        case OP_ST:
            cpuj_mem_write32(cpu, cpu->r[rs] + simm, cpu->r[rd]);
            break;
        case OP_LDB:
            cpu->r[rd] = cpuj_mem_read(cpu, cpu->r[rs] + simm);
            break;
        case OP_STB:
            cpuj_mem_write(cpu, cpu->r[rs] + simm, (uint8_t)(cpu->r[rd] & 0xFF));
            break;

        case OP_JMP: case OP_JEQ: case OP_JNE:
        case OP_JGT: case OP_JGE: case OP_JLT: case OP_JLE: {
            int cond = op - OP_JMP;   /* OP_JMP=ALWAYS .. OP_JLE=LE */
            if (branch_taken(cpu, cond))
                cpu->pc += (uint32_t)simm;
            break;
        }

        case OP_JMPR:
            cpu->pc = cpu->r[rs];
            break;

        case OP_CALL:
            cpuj_push(cpu, cpu->pc);
            cpu->pc += (uint32_t)simm;
            break;

        case OP_CALLR:
            cpuj_push(cpu, cpu->pc);
            cpu->pc = cpu->r[rs];
            break;

        case OP_PUSH:
            cpuj_push(cpu, cpu->r[rd]);
            break;
        case OP_POP:
            cpu->r[rd] = cpuj_pop(cpu);
            break;
        case OP_RET:
            cpu->pc = cpuj_pop(cpu);
            break;
        case OP_TRAP:
            cpuj_trap(cpu, rd);   /* trap code lives in rd field */
            break;
        case OP_HALT:
            cpu->halted = true;
            break;
        case OP_NOP:
            break;

        default:
            break; /* reserved */
    }
}

void cpuj_tick(cpuj_t *cpu) {
    if (cpu->halted) return;
    cpuj_execute(cpu, cpuj_fetch(cpu));
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

int cpuj_disassemble(uint32_t ins, char *buf, int bufsize) {
    int op = CPUJ_OP(ins), rd = CPUJ_RD(ins), rs = CPUJ_RS(ins);
    uint32_t imm = CPUJ_IMM(ins);
    int32_t simm = CPUJ_SIMM(ins);
    int n = 0;

    switch (op) {
        case OP_MOV:  n = emit(buf, bufsize, n, "MOV %s, %s", reg_name(rd), reg_name(rs)); break;
        case OP_MOVI: n = emit(buf, bufsize, n, "MOVI %s, #%u", reg_name(rd), imm); break;
        case OP_MOVU: n = emit(buf, bufsize, n, "MOVU %s, #%u", reg_name(rd), imm & 0xFFF); break;

        case OP_ADD: case OP_ADDI: case OP_SUB: case OP_SUBI:
        case OP_AND: case OP_ANDI: case OP_OR: case OP_ORI:
        case OP_XOR: case OP_XORI: case OP_CMP: case OP_CMPI: {
            static const char *tab[17] = {
                [0]  = "ADD", [1]  = "ADDI", [2] = "SUB", [3] = "SUBI",
                [4]  = "AND", [5]  = "ANDI", [6] = "OR",  [7] = "ORI",
                [8]  = "XOR", [9]  = "XORI",
                [15] = "CMP", [16] = "CMPI",
            };
            const char *mn = tab[op - OP_ADD];
            if (op == OP_ADD || op == OP_SUB || op == OP_AND ||
                op == OP_OR  || op == OP_XOR || op == OP_CMP)
                n = emit(buf, bufsize, n, "%s %s, %s", mn, reg_name(rd), reg_name(rs));
            else
                n = emit(buf, bufsize, n, "%s %s, #%u", mn, reg_name(rd), imm);
            break;
        }

        case OP_NOT:
            n = emit(buf, bufsize, n, "NOT %s", reg_name(rd));
            break;

        case OP_SHL: case OP_SHLI: case OP_SHR: case OP_SHRI: {
            const char *mn = (op == OP_SHL || op == OP_SHLI) ? "SHL" : "SHR";
            if (op == OP_SHL || op == OP_SHR)
                n = emit(buf, bufsize, n, "%s %s, %s", mn, reg_name(rd), reg_name(rs));
            else
                n = emit(buf, bufsize, n, "%sI %s, #%u", mn, reg_name(rd), imm & 31);
            break;
        }

        case OP_LD: case OP_ST: case OP_LDB: case OP_STB: {
            const char *mn = op == OP_LD ? "LD" : op == OP_ST ? "ST" :
                             op == OP_LDB ? "LDB" : "STB";
            if (op == OP_LD || op == OP_LDB)
                n = emit(buf, bufsize, n, "%s %s, [%s%+d]", mn, reg_name(rd), reg_name(rs), simm);
            else
                n = emit(buf, bufsize, n, "%s [%s%+d], %s", mn, reg_name(rs), simm, reg_name(rd));
            break;
        }

        case OP_JMP: case OP_JEQ: case OP_JNE:
        case OP_JGT: case OP_JGE: case OP_JLT: case OP_JLE: {
            static const char *mn[7] = { "JMP","JEQ","JNE","JGT","JGE","JLT","JLE" };
            n = emit(buf, bufsize, n, "%s %+d", mn[op - OP_JMP], simm);
            break;
        }
        case OP_JMPR:  n = emit(buf, bufsize, n, "JMPR %s", reg_name(rs)); break;
        case OP_CALL:  n = emit(buf, bufsize, n, "CALL %+d", simm); break;
        case OP_CALLR: n = emit(buf, bufsize, n, "CALLR %s", reg_name(rs)); break;

        case OP_PUSH:  n = emit(buf, bufsize, n, "PUSH %s", reg_name(rd)); break;
        case OP_POP:   n = emit(buf, bufsize, n, "POP %s", reg_name(rd)); break;
        case OP_RET:   n = emit(buf, bufsize, n, "RET"); break;
        case OP_TRAP:  n = emit(buf, bufsize, n, "TRAP %d", rd); break;
        case OP_HALT:  n = emit(buf, bufsize, n, "HALT"); break;
        case OP_NOP:   n = emit(buf, bufsize, n, "NOP"); break;

        default:
            n = emit(buf, bufsize, n, "??");
            break;
    }
    return n;
}