#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dbg.h"

static uint16_t fetch_word(const cpuj1_t *cpu, uint16_t addr) {
    uint8_t hi = cpu->ram[addr & 0xFF];
    uint8_t lo = cpu->ram[(addr + 1) & 0xFF];
    return ((uint16_t)hi << 8) | lo;
}

void dbg_print_regs(const cpuj1_t *cpu) {
    printf("R0=%02X (%3u)  R1=%02X (%3u)  R2=%02X (%3u)  R3=%02X (%3u)\n",
           cpu->r[0], cpu->r[0],
           cpu->r[1], cpu->r[1],
           cpu->r[2], cpu->r[2],
           cpu->r[3], cpu->r[3]);
    printf("PC=%02X   SP=%02X   FLAGS=%s%s%s%s\n",
           cpu->pc & 0xFF, cpu->sp,
           (cpu->flags & CPUJ1_FLAG_Z) ? "Z " : "",
           (cpu->flags & CPUJ1_FLAG_C) ? "C " : "",
           (cpu->flags & CPUJ1_FLAG_N) ? "N " : "",
           cpu->halted ? "HALT" : "");
}

void dbg_print_mem(const cpuj1_t *cpu, uint16_t from, uint16_t to) {
    for (uint16_t a = from; a <= to && a < CPUJ1_RAM_SIZE; a += 4) {
        printf("0x%02X: ", a & 0xFF);
        for (int i = 0; i < 4 && a + i <= to && a + i < CPUJ1_RAM_SIZE; i++)
            printf("%02X ", cpu->ram[(a + i) & 0xFF]);
        printf("\n");
    }
}

void dbg_print_insn(const cpuj1_t *cpu, uint16_t addr) {
    int pc = addr & 0xFF;
    uint16_t raw = fetch_word(cpu, pc);
    char desc[64];
    cpuj1_disassemble(raw, desc, sizeof desc);
    printf("  0x%02X: %04X    %s\n", pc, raw, desc);
}

void dbg_print_state(const cpuj1_t *cpu) {
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
        if (dbg->breakpoints[i] == (pc & 0xFF)) return true;
    return false;
}

static void step_once(cpuj1_t *cpu, dbg_t *dbg) {
    if (cpu->halted) return;
    dbg_print_insn(cpu, cpu->pc);
    cpuj1_tick(cpu);
    dbg->traps++;
}

static int parse_addr(const char *s, uint16_t *out) {
    if (!s) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 0);
    if (end == s || *end != '\0' || v < 0 || v > 0xFF) return -1;
    *out = (uint16_t)v;
    return 0;
}

/* ── Command loop ──────────────────────────────────────────────────── */

bool dbg_run(cpuj1_t *cpu, const asm_result_t *prog, dbg_t *dbg) {
    char line[128];

    printf("cpuj1 debugger — type 'h' for help, 'q' to quit\n");
    for (;;) {
        if (cpu->halted) {
            printf("program halted.\n");
            dbg_print_state(cpu);
            return true;
        }

        printf("cpuj1> ");
        if (!fgets(line, sizeof line, stdin)) {
            printf("\n");
            return false;
        }

        /* strip trailing newline / comment */
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
                   "  m [lo] [hi] dump memory (default 0x00..0x1F)\n"
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
                    printf("breakpoint hit at 0x%02X\n", cpu->pc & 0xFF);
                    break;
                }
                cpuj1_tick(cpu);
                dbg->traps++;
            }
        } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "regs") == 0) {
            dbg_print_state(cpu);
        } else if (strcmp(cmd, "m") == 0 || strcmp(cmd, "mem") == 0) {
            char *lo = strtok(NULL, " \t");
            char *hi = strtok(NULL, " \t");
            uint16_t a = 0, b = 0x1F;
            if (lo) { uint16_t tmp; if (parse_addr(lo, &tmp) == 0) a = tmp; }
            if (hi) { uint16_t tmp; if (parse_addr(hi, &tmp) == 0) b = tmp; }
            if (b < a) { uint16_t t = a; a = b; b = t; }
            dbg_print_mem(cpu, a, b);
        } else if (strcmp(cmd, "d") == 0 || strcmp(cmd, "disasm") == 0) {
            char *arg = strtok(NULL, " \t");
            uint16_t a = cpu->pc & 0xFF;
            if (arg) { uint16_t tmp; if (parse_addr(arg, &tmp) == 0) a = tmp; }
            for (int i = 0; i < 5; i++) {
                dbg_print_insn(cpu, a);
                a = (a + 2) & 0xFF;
            }
        } else if (strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0) {
            char *arg = strtok(NULL, " \t");
            uint16_t a;
            if (arg && parse_addr(arg, &a) == 0) {
                dbg_add_breakpoint(dbg, a);
                printf("breakpoint set at 0x%02X\n", a);
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
                    printf("breakpoint cleared at 0x%02X\n", a);
                else
                    printf("no breakpoint at that address\n");
            }
        } else if (strcmp(cmd, "l") == 0 || strcmp(cmd, "breaks") == 0) {
            if (dbg->nbreak == 0) {
                printf("no breakpoints\n");
            } else {
                for (int i = 0; i < dbg->nbreak; i++)
                    printf("  0x%02X\n", dbg->breakpoints[i]);
            }
        } else {
            printf("unknown command: %s (try 'h')\n", cmd);
        }
    }
}