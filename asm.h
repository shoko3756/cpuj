#ifndef CPUJ1_ASM_H
#define CPUJ1_ASM_H

#include <stdint.h>
#include <stdbool.h>

#define ASM_MAX_INSTR  2048   /* max instructions in one program */
#define ASM_MAX_LABEL   64
#define ASM_MAX_LABELS  256

typedef struct {
    char name[ASM_MAX_LABEL];
    uint16_t addr;  /* instruction address (byte offset) */
} asm_label_t;

typedef struct {
    uint16_t words[ASM_MAX_INSTR];
    int      count;                 /* number of instructions assembled */
    int      total_bytes;
    asm_label_t labels[ASM_MAX_LABELS];
    int      nlabels;
    char     error[256];
} asm_result_t;

/* Assemble a text program into machine code.
 * Returns true on success; on failure populates asm_result.error. */
bool asm_assemble(const char *source, asm_result_t *res);

#endif /* CPUJ1_ASM_H */