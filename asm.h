#ifndef CPUJ_ASM_H
#define CPUJ_ASM_H

#include <stdint.h>
#include <stdbool.h>

#define ASM_MAX_LABEL   64
#define ASM_MAX_LABELS  256
#define ASM_MAX_CODE    8192   /* max assembled program size in bytes */

typedef struct {
    char name[ASM_MAX_LABEL];
    uint16_t addr;  /* byte address of the label */
} asm_label_t;

typedef struct {
    uint8_t  code[ASM_MAX_CODE];
    int      nbytes;
    asm_label_t labels[ASM_MAX_LABELS];
    int      nlabels;
    char     error[256];
} asm_result_t;

/* Assemble a text program into machine code (big-endian bytes).
 * Returns true on success; on failure populates asm_result.error. */
bool asm_assemble(const char *source, asm_result_t *res);

#endif /* CPUJ_ASM_H */