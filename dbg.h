#ifndef CPUJ1_DBG_H
#define CPUJ1_DBG_H

#include "cpuj1.h"
#include "asm.h"

#define DBG_MAX_BREAKPOINTS 16

typedef struct {
    uint16_t breakpoints[DBG_MAX_BREAKPOINTS]; /* instruction addresses */
    int      nbreak;
    bool     verbose;   /* print executed instructions */
    int      traps;     /* instruction count so far */
} dbg_t;

/* Run the debugger. `imem` is the assembled program (may be NULL).
 * Returns true if the program was stepped completely (no interactive
 * quit); false if the user aborted. */
bool dbg_run(cpuj1_t *cpu, const asm_result_t *prog, dbg_t *dbg);

/* Non-interactive helpers shared with the repl */
void dbg_add_breakpoint(dbg_t *dbg, uint16_t addr);
bool dbg_remove_breakpoint(dbg_t *dbg, uint16_t addr);
void dbg_print_state(const cpuj1_t *cpu);
void dbg_print_regs(const cpuj1_t *cpu);
void dbg_print_mem(const cpuj1_t *cpu, uint16_t from, uint16_t to);
void dbg_print_insn(const cpuj1_t *cpu, uint16_t addr);

#endif /* CPUJ1_DBG_H */