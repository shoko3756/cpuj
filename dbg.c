#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbg.h"

static uint16_t fetch_word(const cpuj_t *cpu, uint16_t addr) {
    uint8_t hi = cpu->ram[addr];
    uint8_t lo = cpu->ram[(uint16_t)(addr + 1)];
    return ((uint16_t)hi << 8) | lo;
}

static uint32_t fetch_instr(const cpuj_t *cpu, uint16_t addr, int *len) {
    uint16_t w1 = fetch_word(cpu, addr);
    uint32_t ins = (uint32_t)w1 << 16;
    if ((w1 & 0x3F) == IMM_LONG) {
        uint16_t w2 = fetch_word(cpu, (uint16_t)(addr + 2));
        ins |= w2;
        *len = 4;
    } else {
        *len = 2;
    }
    return ins;
}

void dbg_print_regs(const cpuj_t *cpu) {
    for (int i = 0; i < CPUJ_REG_COUNT; i++) {
        printf("R%d=%04X (%5u)  %s", i, cpu->r[i], cpu->r[i],
               (i % 2) ? "\n" : "");
    }
    printf("SP=%04X  PC=%04X  FLAGS=%s%s%s%s%s",
           cpu->sp, cpu->pc,
           (cpu->flags & CPUJ_FLAG_Z) ? "Z " : "",
           (cpu->flags & CPUJ_FLAG_C) ? "C " : "",
           (cpu->flags & CPUJ_FLAG_N) ? "N " : "",
           (cpu->flags & CPUJ_FLAG_V) ? "V " : "",
           cpu->halted ? "HALT" : "");
    printf("\n");
}

void dbg_print_mem(const cpuj_t *cpu, uint16_t from, uint16_t to) {
    for (uint16_t a = from; a <= to; a += 16) {
        printf("%04X:", a);
        for (int i = 0; i < 16 && a + i <= to; i++)
            printf(" %02X", cpu->ram[a + i]);
        printf("\n");
    }
}

void dbg_print_insn(const cpuj_t *cpu, uint16_t addr) {
    int len;
    uint32_t ins = fetch_instr(cpu, addr, &len);
    char desc[80];
    cpuj_disassemble(ins, desc, sizeof desc);
    printf("  %04X: ", addr);
    for (int i = 0; i < len; i++)
        printf("%02X", cpu->ram[(uint16_t)(addr + i)]);
    for (int i = len; i < 4; i++)
        printf("  ");
    printf("  %s\n", desc);
}

void dbg_print_state(const cpuj_t *cpu) {
    dbg_print_regs(cpu);
}

void dbg_add_breakpoint(dbg_t *dbg, uint16_t addr) {
    for (int i = 0; i < dbg->nbreak; i++)
        if (dbg->breakpoints[i] == addr) return;
    if (dbg->nbreak < DBG_MAX_BREAKPOINTS)
        dbg->breakpoints[dbg->nbreak++] = addr;
}

bool dbg_remove_breakpoint(dbg_t *dbg, uint16_t addr) {
    for (int i = 0; i < dbg->nbreak; i++) {
        if (dbg->breakpoints[i] == addr) {
            dbg->breakpoints[i] = dbg->breakpoints[dbg->nbreak - 1];
            dbg->nbreak--;
            return true;
        }
    }
    return false;
}

static bool at_breakpoint(const dbg_t *dbg, uint16_t pc) {
    for (int i = 0; i < dbg->nbreak; i++)
        if (dbg->breakpoints[i] == pc) return true;
    return false;
}

static void step_once(cpuj_t *cpu, dbg_t *dbg) {
    if (cpu->halted) return;
    dbg_print_insn(cpu, cpu->pc);
    cpuj_tick(cpu);
    dbg->cycles++;
}

static int parse_addr(const char *s, uint16_t *out) {
    if (!s) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s || *end != '\0' || v < 0 || v > 0xFFFF) return -1;
    *out = (uint16_t)v;
    return 0;
}

/* ── Command loop ──────────────────────────────────────────────────── */

bool dbg_run(cpuj_t *cpu, const asm_result_t *prog, dbg_t *dbg) {
    char line[128];

    printf("cpuj debugger — type 'h' for help, 'q' to quit\n");
    for (;;) {
        if (cpu->halted) {
            printf("program halted after %d cycles.\n", dbg->cycles);
            dbg_print_state(cpu);
            return true;
        }

        printf("cpuj> ");
        if (!fgets(line, sizeof line, stdin)) {
            printf("\n");
            return false;
        }

        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[len - 1] = '\0';
        char *p = strchr(line, ';');
        if (p) *p = '\0';

        char *cmd = strtok(line, " \t");
        if (!cmd) continue;

        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("  s [n]      step n instructions (default 1)\n"
                   "  c          continue until halt or breakpoint\n"
                   "  r          show registers / flags\n"
                   "  m [lo] [hi] dump memory (default 0x0000..0x000F)\n"
                   "  d [addr]   disassemble around addr (default PC)\n"
                   "  b addr     set breakpoint\n"
                   "  x addr     clear breakpoint (all if no addr)\n"
                   "  l          list breakpoints\n"
                   "  q          quit\n"
                   "  h          this help\n");
        } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            printf("bye.\n");
            return false;
        } else if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
            char *arg = strtok(NULL, " \t");
            int n = 1;
            if (arg) n = atoi(arg);
            if (n < 1) n = 1;
            for (int i = 0; i < n && !cpu->halted; i++)
                step_once(cpu, dbg);
        } else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "continue") == 0) {
            for (;;) {
                if (cpu->halted) break;
                if (at_breakpoint(dbg, cpu->pc)) {
                    printf("breakpoint hit at 0x%04X\n", cpu->pc);
                    break;
                }
                cpuj_tick(cpu);
                dbg->cycles++;
            }
        } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "regs") == 0) {
            dbg_print_state(cpu);
        } else if (strcmp(cmd, "m") == 0 || strcmp(cmd, "mem") == 0) {
            char *lo = strtok(NULL, " \t");
            char *hi = strtok(NULL, " \t");
            uint16_t a = 0, b = 0x0F;
            if (lo) { uint16_t tmp; if (parse_addr(lo, &tmp) == 0) a = tmp; }
            if (hi) { uint16_t tmp; if (parse_addr(hi, &tmp) == 0) b = tmp; }
            if (b < a) { uint16_t t = a; a = b; b = t; }
            dbg_print_mem(cpu, a, b);
        } else if (strcmp(cmd, "d") == 0 || strcmp(cmd, "disasm") == 0) {
            char *arg = strtok(NULL, " \t");
            uint16_t a = cpu->pc;
            if (arg) { uint16_t tmp; if (parse_addr(arg, &tmp) == 0) a = tmp; }
            for (int i = 0; i < 5; i++) {
                dbg_print_insn(cpu, a);
                int len;
                (void)fetch_instr(cpu, a, &len);
                a = (uint16_t)(a + len);
            }
        } else if (strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0) {
            char *arg = strtok(NULL, " \t");
            uint16_t a;
            if (arg && parse_addr(arg, &a) == 0) {
                dbg_add_breakpoint(dbg, a);
                printf("breakpoint set at 0x%04X\n", a);
            } else {
                printf("usage: b addr\n");
            }
        } else if (strcmp(cmd, "x") == 0 || strcmp(cmd, "clear") == 0) {
            char *arg = strtok(NULL, " \t");
            if (!arg) {
                dbg->nbreak = 0;
                printf("all breakpoints cleared\n");
            } else {
                uint16_t a;
                if (parse_addr(arg, &a) == 0 && dbg_remove_breakpoint(dbg, a))
                    printf("breakpoint cleared at 0x%04X\n", a);
                else
                    printf("no breakpoint at that address\n");
            }
        } else if (strcmp(cmd, "l") == 0 || strcmp(cmd, "breaks") == 0) {
            if (dbg->nbreak == 0) {
                printf("no breakpoints\n");
            } else {
                for (int i = 0; i < dbg->nbreak; i++)
                    printf("  0x%04X\n", dbg->breakpoints[i]);
            }
        } else {
            printf("unknown command: %s (try 'h')\n", cmd);
        }
    }
}