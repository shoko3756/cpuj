#ifndef CPUJ_DBG_H
#define CPUJ_DBG_H

#include "cpuj.h"
#include "asm.h"

#define DBG_MAX_BREAKPOINTS 16

typedef struct {
    uint32_t breakpoints[DBG_MAX_BREAKPOINTS]; /* byte addresses */
    int      nbreak;
    int      cycles;   /* cycle count since debugger started */
} dbg_t;

/* Run the debugger. `prog` is the assembled program (may be NULL for
 * raw-binary loads). Returns true if the program ran to completion,
 * false if the user quit. */
bool dbg_run(cpuj_t *cpu, const asm_result_t *prog, dbg_t *dbg);

void dbg_add_breakpoint(dbg_t *dbg, uint32_t addr);
bool dbg_remove_breakpoint(dbg_t *dbg, uint32_t addr);
void dbg_print_state(const cpuj_t *cpu);
void dbg_print_regs(const cpuj_t *cpu);
void dbg_print_mem(const cpuj_t *cpu, uint32_t from, uint32_t to);
void dbg_print_insn(const cpuj_t *cpu, uint32_t addr);

#endif /* CPUJ_DBG_H */