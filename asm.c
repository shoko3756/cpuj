#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include "asm.h"
#include "cpuj1.h"

/* ── Small parsing helpers ─────────────────────────────────────────── */

static int line_num = 0;

static void set_err(asm_result_t *res, const char *fmt, ...) {
    int n = snprintf(res->error, sizeof res->error, "line %d: ", line_num);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->error + n, sizeof res->error - (size_t)n, fmt, ap);
    va_end(ap);
}

/* Decode a number token: strips a leading '#' and supports
 * decimal, 0x hex, 0b binary, octal (0nnn). Returns 0 on failure. */
static bool parse_num(const char *tok, int *out) {
    while (*tok == ' ' || *tok == '\t') tok++;
    if (*tok == '#') tok++;
    if (*tok == '\0') return false;

    char *end = NULL;
    long v = 0;
    if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
        v = strtol(tok + 2, &end, 16);
    else if (tok[0] == '0' && (tok[1] == 'b' || tok[1] == 'B'))
        v = strtol(tok + 2, &end, 2);
    else if (tok[0] == '0' && tok[1] != '\0')
        v = strtol(tok, &end, 8);
    else
        v = strtol(tok, &end, 10);

    if (end == tok || *end != '\0') return false;
    if (v < 0 || v > 0xFFFF) return false;
    *out = (int)v;
    return true;
}

static bool is_reg(const char *tok, int *reg) {
    size_t len = strlen(tok);
    if (len != 2 || (tok[0] != 'R' && tok[0] != 'r')) return false;
    char c = tok[1];
    if (c < '0' || c > '3') return false;
    *reg = c - '0';
    return true;
}

/* Parse a bare register token like "R0" (no brackets). */
static bool parse_reg_tok(const char *tok, int *reg) {
    char buf[16];
    const char *p = tok;
    while (*p && *p == ' ') p++;
    size_t len = 0;
    while (*p && *p != ' ' && *p != ',' && len < 15) buf[len++] = *p++;
    buf[len] = '\0';
    return is_reg(buf, reg);
}

/* Extract a register or address from an operand token that may be
 * wrapped in brackets, e.g. "[R1]" or "[0x40]" or "[@data]". */
typedef enum { OPR_REG, OPR_ADDR, OPR_LABEL } opr_kind_t;

typedef struct {
    opr_kind_t kind;
    int   value;      /* register idx or address */
    char  label[ASM_MAX_LABEL];
} opr_t;

static bool parse_operand(const char *tok, opr_t *out) {
    char buf[ASM_MAX_LABEL];
    const char *p = tok;
    while (*p && *p == ' ') p++;

    if (*p != '[') return false;
    p++;
    size_t len = 0;
    while (*p && *p != ']' && len < sizeof buf - 1) buf[len++] = *p++;
    buf[len] = '\0';
    if (*p != ']') return false;

    int r, v;
    if (is_reg(buf, &r)) {
        out->kind = OPR_REG;
        out->value = r;
        return true;
    }
    if (parse_num(buf, &v)) {
        out->kind = OPR_ADDR;
        out->value = v;
        return true;
    }
    if (buf[0] == '@') {
        out->kind = OPR_LABEL;
        snprintf(out->label, sizeof out->label, "%s", buf + 1);
        return true;
    }
    return false;
}

/* Parse an operand that is either a register or an immediate */
typedef enum { SRC_REG, SRC_IMM, SRC_LABEL } src_kind_t;

typedef struct {
    src_kind_t kind;
    int   value;
    char  label[ASM_MAX_LABEL];
} src_t;

static bool parse_src(const char *tok, src_t *out) {
    char buf[ASM_MAX_LABEL];
    const char *p = tok;
    while (*p == ' ') p++;
    size_t len = 0;
    while (*p && *p != ',' && len < sizeof buf - 1) buf[len++] = *p++;
    buf[len] = '\0';

    if (len == 0) return false;

    int r;
    if (is_reg(buf, &r)) {
        out->kind = SRC_REG;
        out->value = r;
        return true;
    }
    int v;
    if (parse_num(buf, &v)) {
        out->kind = SRC_IMM;
        out->value = v;
        if (v > 0xFF) return false;
        return true;
    }
    if (buf[0] == '@') {
        out->kind = SRC_LABEL;
        snprintf(out->label, sizeof out->label, "%s", buf + 1);
        return true;
    }
    return false;
}

/* ── Two-pass assembler ────────────────────────────────────────────── */

typedef struct {
    char  mnemonic[8];
    int   has_op1, has_op2;   /* whether operand slot is present at all */
    opr_t op1;
    opr_t op2;
    src_t s1, s2;             /* src-style parses of the operands */
} inst_line_t;

static int tokenize_line(const char *line, char **toks, int maxtoks) {
    int n = 0;
    const char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == ';') break;
        if (n >= maxtoks) return -1;
        /* because operands can look like "[R1], R2", split on commas too */
        size_t len = 0;
        char tmp[ASM_MAX_LABEL];
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';') {
            if (len < sizeof tmp - 1) tmp[len++] = *p;
            p++;
        }
        tmp[len] = '\0';
        if (len == 0 && *p == ',') { p++; continue; }
        toks[n++] = strdup(tmp);
        if (*p == ',') p++;
        else while (*p == ' ') p++;
    }
    return n;
}

bool asm_assemble(const char *source, asm_result_t *res) {
    memset(res, 0, sizeof *res);

    /* First pass: find labels. Instructions are 2 bytes each. */
    int pc = 0; /* instruction address in bytes */
    {
        const char *p = source;
        line_num = 0;
        while (*p) {
            line_num++;
            const char *eol = strchr(p, '\n');
            size_t llen = eol ? (size_t)(eol - p) : strlen(p);

            char *line = malloc(llen + 1);
            memcpy(line, p, llen);
            line[llen] = '\0';
            p = eol ? eol + 1 : p + llen;

            char *line_cpy = strdup(line);
            char *label = strtok(line_cpy, " \t"); /* first token */
            size_t ll = label ? strlen(label) : 0;

            int is_label = ll > 0 && label[ll - 1] == ':';
            int has_instr = label && !is_label && label[0] != ';' && label[0] != '\0';

            if (is_label) {
                char name[ASM_MAX_LABEL];
                size_t nlen = ll - 1;
                if (nlen >= ASM_MAX_LABEL) nlen = ASM_MAX_LABEL - 1;
                memcpy(name, label, nlen);
                name[nlen] = '\0';
                if (res->nlabels < ASM_MAX_LABELS) {
                    snprintf(res->labels[res->nlabels].name,
                             sizeof res->labels[res->nlabels].name,
                             "%s", name);
                    res->labels[res->nlabels].addr = (uint16_t)pc;
                    res->nlabels++;
                }
                /* label may have an instruction after it on the same line */
                has_instr = false;
                char *rest = line_cpy + ll;
                while (*rest && *rest != ' ' && *rest != '\t') rest++;
                if (*rest) {
                    while (*rest == ' ' || *rest == '\t') rest++;
                    if (*rest && *rest != ';') has_instr = true;
                }
                (void)rest;
            }

            if (has_instr && !is_label) {
                /* only count instruction lines */
                char *tok = label;
                if (strcmp(tok, ";") != 0 && tok[0] != ';' && tok[0] != '\0') {
                    pc += 2;
                }
            }

            free(line_cpy);
            free(line);
        }
    }

    /* Second pass: assemble. */
    pc = 0;
    {
        const char *p = source;
        line_num = 0;
        #define MAXTOKS 8
        while (*p) {
            line_num++;
            const char *eol = strchr(p, '\n');
            size_t llen = eol ? (size_t)(eol - p) : strlen(p);

            char *line = malloc(llen + 1);
            memcpy(line, p, llen);
            line[llen] = '\0';
            p = eol ? eol + 1 : p + llen;

            char *toks[MAXTOKS];
            for (int i = 0; i < MAXTOKS; i++) toks[i] = NULL;
            int ntoks = tokenize_line(line, toks, MAXTOKS);

            int base = 0;
            /* skip leading label like "name:" */
            if (ntoks > 0 && toks[0][strlen(toks[0]) - 1] == ':') base = 1;

            if (ntoks > base) {
                const char *mn = toks[base];
                uint16_t word = 0;
                bool ok = true;

                if (strcmp(mn, "HALT") == 0 || strcmp(mn, "halt") == 0) {
                    word = CPUJ1_HALT();
                } else if (strcmp(mn, "NOP") == 0 || strcmp(mn, "nop") == 0) {
                    word = CPUJ1_NOP();
                } else if (strcmp(mn, "RET") == 0 || strcmp(mn, "ret") == 0) {
                    word = CPUJ1_RET();
                } else if (strcmp(mn, "MOV") == 0 || strcmp(mn, "mov") == 0) {
                    int rd;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "MOV needs register destinations");
                        ok = false; goto asm_err;
                    }
                    src_t s;
                    if (!parse_src(toks[base+2], &s) || s.kind == SRC_LABEL) {
                        set_err(res, "MOV expects register source"); ok = false; goto asm_err;
                    }
                    if (s.kind == SRC_REG) word = CPUJ1_MOV(rd, s.value);
                    else { word = CPUJ1_MOVI(rd, (uint8_t)s.value); }
                } else if (strcmp(mn, "MOVI") == 0 || strcmp(mn, "movi") == 0) {
                    int rd;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "MOVI needs a register"); ok = false; goto asm_err;
                    }
                    src_t s;
                    if (!parse_src(toks[base+2], &s) || s.kind != SRC_IMM) {
                        set_err(res, "MOVI expects #imm"); ok = false; goto asm_err;
                    }
                    word = CPUJ1_MOVI(rd, (uint8_t)s.value);
                } else if (strcmp(mn, "ADD") == 0 || strcmp(mn, "add") == 0 ||
                           strcmp(mn, "SUB") == 0 || strcmp(mn, "sub") == 0 ||
                           strcmp(mn, "AND") == 0 || strcmp(mn, "and") == 0 ||
                           strcmp(mn, "OR") == 0  || strcmp(mn, "or") == 0 ||
                           strcmp(mn, "XOR") == 0 || strcmp(mn, "xor") == 0 ||
                           strcmp(mn, "CMP") == 0 || strcmp(mn, "cmp") == 0) {
                    int op = OP_ADD;
                    if (strcasecmp(mn, "SUB") == 0) op = OP_SUB;
                    else if (strcasecmp(mn, "AND") == 0) op = OP_AND;
                    else if (strcasecmp(mn, "OR") == 0) op = OP_OR;
                    else if (strcasecmp(mn, "XOR") == 0) op = OP_XOR;
                    else if (strcasecmp(mn, "CMP") == 0) op = OP_CMP;

                    int rd;
                    src_t s;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "%s needs a destination register", mn);
                        ok = false; goto asm_err;
                    }
                    if (!parse_src(toks[base+2], &s) || s.kind == SRC_LABEL) {
                        set_err(res, "%s needs a source operand", mn);
                        ok = false; goto asm_err;
                    }
                    if (s.kind == SRC_REG) {
                        word = CPUJ1_ENC((uint16_t)op, (uint16_t)rd, (uint16_t)s.value, 0);
                    } else {
                        if (s.value > 0xFF) { set_err(res, "immediate too large"); ok = false; goto asm_err; }
                        switch (op) {
                            case OP_ADD: word = CPUJ1_ADD_IMM(rd, s.value); break;
                            case OP_SUB: word = CPUJ1_SUB_IMM(rd, s.value); break;
                            case OP_AND: word = CPUJ1_AND_IMM(rd, s.value); break;
                            case OP_OR:  word = CPUJ1_OR_IMM(rd, s.value);  break;
                            case OP_XOR: word = CPUJ1_XOR_IMM(rd, s.value); break;
                            case OP_CMP: word = CPUJ1_CMP_IMM(rd, s.value); break;
                        }
                    }
                } else if (strcmp(mn, "NOT") == 0 || strcmp(mn, "not") == 0) {
                    int rd;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "NOT needs a register"); ok = false; goto asm_err;
                    }
                    word = CPUJ1_NOT(rd);
                } else if (strcmp(mn, "SHL") == 0 || strcmp(mn, "shl") == 0 ||
                           strcmp(mn, "SHR") == 0 || strcmp(mn, "shr") == 0) {
                    int rd;
                    src_t s;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "SHL/SHR need a register"); ok = false; goto asm_err;
                    }
                    if (!parse_src(toks[base+2], &s) || s.kind != SRC_IMM) {
                        set_err(res, "SHL/SHR need a shift amount"); ok = false; goto asm_err;
                    }
                    if (strcasecmp(mn, "SHL") == 0)
                        word = CPUJ1_SHL(rd, s.value);
                    else
                        word = CPUJ1_SHR(rd, s.value);
                } else if (strcmp(mn, "LD") == 0 || strcmp(mn, "ld") == 0) {
                    int rd;
                    opr_t op2;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "LD needs a destination register"); ok = false; goto asm_err;
                    }
                    if (!parse_operand(toks[base+2], &op2)) {
                        set_err(res, "LD needs [addr] or [Ri]"); ok = false; goto asm_err;
                    }
                    if (op2.kind == OPR_REG) word = CPUJ1_LD_PTR(rd, op2.value);
                    else if (op2.kind == OPR_ADDR) word = CPUJ1_LD_ABS(rd, op2.value);
                    else word = 0; /* label reference handled later */
                } else if (strcmp(mn, "ST") == 0 || strcmp(mn, "st") == 0) {
                    opr_t op1;
                    int rs;
                    if (!parse_operand(toks[base+1], &op1)) {
                        set_err(res, "ST needs [addr] or [Ri]"); ok = false; goto asm_err;
                    }
                    if (!parse_reg_tok(toks[base+2], &rs)) {
                        set_err(res, "ST needs a source register"); ok = false; goto asm_err;
                    }
                    if (op1.kind == OPR_REG) word = CPUJ1_ST_PTR(op1.value, rs);
                    else if (op1.kind == OPR_ADDR) word = CPUJ1_ST_ABS(op1.value, rs);
                    else word = 0;
                } else if (strcmp(mn, "PUSH") == 0 || strcmp(mn, "push") == 0) {
                    int rs;
                    if (!parse_reg_tok(toks[base+1], &rs)) {
                        set_err(res, "PUSH needs a register"); ok = false; goto asm_err;
                    }
                    word = CPUJ1_PUSH(rs);
                } else if (strcmp(mn, "POP") == 0 || strcmp(mn, "pop") == 0) {
                    int rd;
                    if (!parse_reg_tok(toks[base+1], &rd)) {
                        set_err(res, "POP needs a register"); ok = false; goto asm_err;
                    }
                    word = CPUJ1_POP(rd);
                } else if (strcmp(mn, "JMP") == 0 || strcmp(mn, "jmp") == 0 ||
                           strcmp(mn, "JEQ") == 0 || strcmp(mn, "jeq") == 0 ||
                           strcmp(mn, "JNE") == 0 || strcmp(mn, "jne") == 0 ||
                           strcmp(mn, "JGT") == 0 || strcmp(mn, "jgt") == 0) {
                    src_t s;
                    if (!parse_src(toks[base+1], &s)) {
                        set_err(res, "%s needs a target", mn); ok = false; goto asm_err;
                    }
                    int cond = JMP_ALWAYS;
                    if (mn[1] == 'E' || mn[1] == 'e') cond = JMP_EQ;
                    else if (mn[1] == 'N' || mn[1] == 'n') cond = JMP_NE;
                    else if (mn[1] == 'G' || mn[1] == 'g') cond = JMP_GT;
                    if (s.kind == SRC_LABEL) {
                        int addr = -1;
                        for (int i = 0; i < res->nlabels; i++)
                            if (strcmp(res->labels[i].name, s.label) == 0)
                                { addr = res->labels[i].addr; break; }
                        if (addr < 0 || addr > 0xFF) {
                            set_err(res, "unknown or out-of-range label '%s'", s.label);
                            ok = false; goto asm_err;
                        }
                        word = CPUJ1_ENC(OP_JMP, 0, (uint16_t)cond, (uint16_t)addr);
                    } else {
                        if (s.value > 0xFF) { set_err(res, "target too large"); ok = false; goto asm_err; }
                        word = CPUJ1_ENC(OP_JMP, 0, (uint16_t)cond, (uint16_t)s.value);
                    }
                } else if (strcmp(mn, "CALL") == 0 || strcmp(mn, "call") == 0) {
                    src_t s;
                    if (!parse_src(toks[base+1], &s)) {
                        set_err(res, "CALL needs a target"); ok = false; goto asm_err;
                    }
                    int addr = -1;
                    if (s.kind == SRC_LABEL) {
                        for (int i = 0; i < res->nlabels; i++)
                            if (strcmp(res->labels[i].name, s.label) == 0)
                                { addr = res->labels[i].addr; break; }
                        if (addr < 0 || addr > 0xFF) {
                            set_err(res, "unknown or out-of-range label '%s'", s.label);
                            ok = false; goto asm_err;
                        }
                    } else addr = s.value;
                    if (addr > 0xFF) { set_err(res, "target too large"); ok = false; goto asm_err; }
                    word = CPUJ1_CALL(addr);
                } else if (strcmp(mn, "TRAP") == 0 || strcmp(mn, "trap") == 0) {
                    const char *name = toks[base+1];
                    int code = -1;
                    src_t s;
                    if (parse_src(name, &s) && s.kind == SRC_IMM) code = s.value;
                    if (code < 0) {
                        /* named traps */
                        if (strcasecmp(name, "PRINT_REG") == 0) code = TRAP_PRINT_REG;
                        else if (strcasecmp(name, "PRINT_CHAR") == 0) code = TRAP_PRINT_CHAR;
                        else if (strcasecmp(name, "READ_CHAR") == 0) code = TRAP_READ_CHAR;
                        else if (strcasecmp(name, "PRINT_STR") == 0) code = TRAP_PRINT_STR;
                        else if (strcasecmp(name, "HALT") == 0) code = TRAP_HALT;
                        else if (strcasecmp(name, "halt") == 0) code = TRAP_HALT;
                    }
                    if (code < 0 || code > 15) {
                        set_err(res, "unknown trap '%s'", name); ok = false; goto asm_err;
                    }
                    word = CPUJ1_TRAP(code);
                } else {
                    set_err(res, "unknown instruction '%s'", mn);
                    ok = false;
                    break;
                }

                asm_err:
                if (!ok) { for (int i = 0; i < MAXTOKS; i++) free(toks[i]); free(line); goto fail; }

                if (res->count < ASM_MAX_INSTR) {
                    res->words[res->count++] = word;
                }
                pc += 2;
            }

            for (int i = 0; i < MAXTOKS; i++) free(toks[i]);
            free(line);
        }
    }

    res->total_bytes = pc;
    return true;

fail:
    return false;
}

#undef MAXTOKS