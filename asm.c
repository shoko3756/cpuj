#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include "asm.h"
#include "cpuj.h"

#define MAXTOKS    8
#define MAXLINES   4096

static int line_num = 0;

static void set_err(asm_result_t *res, const char *fmt, ...) {
    if (res->error[0]) return; /* keep the first error */
    int n = snprintf(res->error, sizeof res->error, "line %d: ", line_num);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->error + n, sizeof res->error - (size_t)n, fmt, ap);
    va_end(ap);
}

/* ── Number / token parsing ────────────────────────────────────────── */

/* Number token: strips leading '#', allows hex/0b/octal/dec and a sign.
 * Range -32768..65535. Returns false on garbage. */
static bool parse_num(const char *tok, long *out) {
    while (*tok == ' ' || *tok == '\t') tok++;
    if (*tok == '#') tok++;
    if (*tok == '\0') return false;

    const char *p = tok;
    long sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') p++;

    char *end = NULL;
    long v;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        v = strtol(p + 2, &end, 16);
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))
        v = strtol(p + 2, &end, 2);
    else if (p[0] == '0' && p[1] != '\0')
        v = strtol(p, &end, 8);
    else
        v = strtol(p, &end, 10);

    if (end == p || *end != '\0') return false;
    v *= sign;
    if (v < -32768 || v > 65535) return false;
    *out = v;
    return true;
}

static bool is_reg(const char *tok, int *reg) {
    if (tok[0] != 'R' && tok[0] != 'r') return false;
    if (tok[1] < '0' || tok[1] > '7') return false;
    if (tok[2] != '\0') return false;
    *reg = tok[1] - '0';
    return true;
}

/* Operand: register, immediate, or @label reference. */
typedef struct {
    int kind;               /* 0=reg, 1=imm, 2=label */
    int reg;
    long imm;
    char label[ASM_MAX_LABEL];
} opr_t;

static bool parse_opr(const char *tok, opr_t *o) {
    char buf[ASM_MAX_LABEL];
    const char *p = tok;
    while (*p == ' ' || *p == '\t') p++;
    size_t len = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != ',' && len < sizeof buf - 1)
        buf[len++] = *p++;
    buf[len] = '\0';
    if (len == 0) return false;

    if (is_reg(buf, &o->reg)) { o->kind = 0; return true; }
    if (parse_num(buf, &o->imm)) { o->kind = 1; return true; }
    if (buf[0] == '@') {
        o->kind = 2;
        snprintf(o->label, sizeof o->label, "%s", buf + 1);
        return true;
    }
    return false;
}

/* Memory operand "[...]" → base register + offset, or absolute (num/@label). */
typedef struct {
    int             is_base;    /* base + offset form */
    int             base;
    long            off;
    int             is_abs_num;
    long            abs;
    int             is_abs_label;
    char            label[ASM_MAX_LABEL];
} memopr_t;

static bool parse_memopr(const char *tok, memopr_t *m) {
    const char *p = tok;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;

    char buf[ASM_MAX_LABEL];
    size_t len = 0;
    while (*p && *p != ']' && len < sizeof buf - 1) buf[len++] = *p++;
    buf[len] = '\0';
    if (*p != ']') return false;
    p++;
    while (*p) { if (*p != ' ' && *p != '\t') return false; p++; }

    memset(m, 0, sizeof *m);

    if (buf[0] == 'R' || buf[0] == 'r') {
        char comp[16];
        size_t clen = 0;
        const char *q = buf;
        while (*q && !isspace((unsigned char)*q) && *q != '+' && *q != '-'
               && clen < sizeof comp - 1)
            comp[clen++] = *q++;
        comp[clen] = '\0';
        int base;
        if (!is_reg(comp, &base)) return false;
        m->is_base = 1;
        m->base = base;
        m->off = 0;
        while (isspace((unsigned char)*q)) q++;
        if (*q == '\0') return true;
        if (!parse_num(q, &m->off)) return false;
        return true;
    }
    if (buf[0] == '@') {
        m->is_abs_label = 1;
        snprintf(m->label, sizeof m->label, "%s", buf + 1);
        return true;
    }
    if (parse_num(buf, &m->abs)) { m->is_abs_num = 1; return true; }
    return false;
}

/* ── Instruction lines ─────────────────────────────────────────────── */

typedef enum {
    LN_MOV, LN_MOVI, LN_ADD, LN_SUB, LN_AND, LN_OR, LN_XOR,
    LN_NOT, LN_SHL, LN_SHR, LN_CMP,
    LN_LD, LN_ST,
    LN_JMP, LN_JEQ, LN_JNE, LN_JGT, LN_JGE, LN_JLT, LN_JLE,
    LN_CALL, LN_PUSH, LN_POP, LN_RET, LN_TRAP, LN_HALT, LN_NOP
} ln_kind_t;

typedef struct {
    int       has_label;
    char      label[ASM_MAX_LABEL];
    int       has_instr;    /* line contains an instruction (vs label-only) */
    ln_kind_t kind;
    char      ops[MAXTOKS][ASM_MAX_LABEL];
    int       nops;
    int       lineno;
    long      target;       /* resolved absolute target / imm */
    int       long_flag;    /* emit in 32-bit long form */
    uint16_t  addr;         /* byte address of instruction start */
    uint16_t  next;         /* byte address after the instruction */
} ln_t;

static ln_t lines[MAXLINES];
static int nlines;
static asm_label_t labels[ASM_MAX_LABELS];
static int nlabels;

static int tokenize(const char *line, char (*toks)[ASM_MAX_LABEL], int maxtoks) {
    int n = 0;
    const char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == ';') break;
        if (n >= maxtoks) return -1;

        char *d = toks[n];
        size_t len = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';') {
            if (len < ASM_MAX_LABEL - 1) d[len++] = *p;
            p++;
        }
        d[len] = '\0';
        if (len > 0) n++;
        if (*p == ',') p++;
    }
    return n;
}

static bool find_label(const char *name, uint16_t *addr) {
    for (int i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0) { *addr = labels[i].addr; return true; }
    return false;
}

static bool add_label(const char *name, uint16_t addr, asm_result_t *res) {
    uint16_t tmp;
    if (find_label(name, &tmp)) {
        set_err(res, "duplicate label '@%s'", name);
        return false;
    }
    if (nlabels >= ASM_MAX_LABELS) {
        set_err(res, "too many labels");
        return false;
    }
    snprintf(labels[nlabels].name, sizeof labels[nlabels].name, "%s", name);
    labels[nlabels].addr = addr;
    nlabels++;
    return true;
}

static bool kind_from_name(const char *mn, ln_kind_t *k) {
    struct { const char *name; ln_kind_t k; } tab[] = {
        { "MOV", LN_MOV }, { "MOVI", LN_MOVI },
        { "ADD", LN_ADD }, { "SUB", LN_SUB }, { "AND", LN_AND },
        { "OR", LN_OR }, { "XOR", LN_XOR }, { "CMP", LN_CMP },
        { "NOT", LN_NOT }, { "SHL", LN_SHL }, { "SHR", LN_SHR },
        { "LD", LN_LD }, { "ST", LN_ST },
        { "JMP", LN_JMP }, { "JEQ", LN_JEQ }, { "JNE", LN_JNE },
        { "JGT", LN_JGT }, { "JGE", LN_JGE }, { "JLT", LN_JLT },
        { "JLE", LN_JLE },
        { "CALL", LN_CALL }, { "PUSH", LN_PUSH }, { "POP", LN_POP },
        { "RET", LN_RET }, { "TRAP", LN_TRAP }, { "HALT", LN_HALT },
        { "NOP", LN_NOP },
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcasecmp(mn, tab[i].name) == 0) { *k = tab[i].k; return true; }
    return false;
}

static const char *mnemonics[LN_NOP + 1] = {
    [LN_MOV] = "MOV", [LN_MOVI] = "MOVI",
    [LN_ADD] = "ADD", [LN_SUB] = "SUB", [LN_AND] = "AND",
    [LN_OR] = "OR", [LN_XOR] = "XOR", [LN_CMP] = "CMP",
    [LN_NOT] = "NOT", [LN_SHL] = "SHL", [LN_SHR] = "SHR",
    [LN_LD] = "LD", [LN_ST] = "ST",
    [LN_JMP] = "JMP", [LN_JEQ] = "JEQ", [LN_JNE] = "JNE",
    [LN_JGT] = "JGT", [LN_JGE] = "JGE", [LN_JLT] = "JLT",
    [LN_JLE] = "JLE",
    [LN_CALL] = "CALL", [LN_PUSH] = "PUSH", [LN_POP] = "POP",
    [LN_RET] = "RET", [LN_TRAP] = "TRAP", [LN_HALT] = "HALT",
    [LN_NOP] = "NOP",
};

static int operand_need(ln_kind_t k) {
    switch (k) {
        case LN_MOV: case LN_MOVI: case LN_ADD: case LN_SUB:
        case LN_AND: case LN_OR: case LN_XOR: case LN_CMP:
        case LN_SHL: case LN_SHR: case LN_LD: case LN_ST:
            return 2;
        case LN_NOT: case LN_PUSH: case LN_POP:
        case LN_JMP: case LN_JEQ: case LN_JNE: case LN_JGT:
        case LN_JGE: case LN_JLT: case LN_JLE:
        case LN_CALL: case LN_TRAP:
            return 1;
        case LN_RET: case LN_HALT: case LN_NOP:
            return 0;
    }
    return -1;
}

/* Instructions whose operands force the 32-bit long form. */
static bool instr_always_long(const ln_t *ln) {
    opr_t o;
    memopr_t m;
    switch (ln->kind) {
        case LN_CALL:
            if (ln->nops < 1) return false;
            if (!parse_opr(ln->ops[0], &o)) return false;
            return o.kind != 0;      /* reg-indirect call is short */
        case LN_MOV: case LN_MOVI:
        case LN_ADD: case LN_SUB: case LN_AND:
        case LN_OR: case LN_XOR: case LN_CMP:
            if (ln->nops < 2) return false;
            if (!parse_opr(ln->ops[1], &o)) return false;
            return o.kind != 0;      /* immediate or @label => long form */
        case LN_LD: case LN_ST: {
            int mi = (ln->kind == LN_LD) ? 1 : 0;
            if (ln->nops < 2) return false;
            if (!parse_memopr(ln->ops[mi], &m)) return false;
            return !m.is_base;       /* absolute address => long form */
        }
        default:
            return false;
    }
}

/* Parse a line into `ln`. Tokens exclude a leading label. */
static bool parse_instr(ln_t *ln, int ntoks, char (*toks)[ASM_MAX_LABEL], asm_result_t *res) {
    if (ntoks < 1) return false;
    if (!kind_from_name(toks[0], &ln->kind)) {
        set_err(res, "unknown instruction '%s'", toks[0]);
        return false;
    }
    int need = operand_need(ln->kind);
    if (ntoks - 1 != need) {
        set_err(res, "%s takes %d operand%s", mnemonics[ln->kind], need,
                need == 1 ? "" : "s");
        return false;
    }
    for (int i = 1; i < ntoks; i++)
        snprintf(ln->ops[i - 1], ASM_MAX_LABEL, "%s", toks[i]);
    ln->nops = ntoks - 1;
    ln->long_flag = instr_always_long(ln) ? 1 : 0;
    return true;
}

/* ── Assembler entry ─────────────────────────────────────────────── */

bool asm_assemble(const char *source, asm_result_t *res) {
    memset(res, 0, sizeof *res);
    nlines = 0;
    nlabels = 0;

    /* Pass 1: tokenize every line, record labels, build line table. */
    line_num = 0;
    {
        const char *p = source;
        while (*p) {
            line_num++;
            if (nlines >= MAXLINES) { set_err(res, "too many lines"); return false; }
            const char *eol = strchr(p, '\n');
            size_t llen = eol ? (size_t)(eol - p) : strlen(p);

            char *line = malloc(llen + 1);
            memcpy(line, p, llen);
            line[llen] = '\0';
            p = eol ? eol + 1 : p + llen;

            ln_t *ln = &lines[nlines];
            memset(ln, 0, sizeof *ln);
            ln->lineno = line_num;
            ln->kind = LN_NOP;

            char toks[MAXTOKS][ASM_MAX_LABEL] = { 0 };
            int ntoks = tokenize(line, toks, MAXTOKS);
            free(line);
            if (ntoks == -1) { set_err(res, "too many tokens on line"); return false; }
            if (ntoks == 0) continue; /* blank / comment */

            /* possible leading label "name:" */
            size_t ll = strlen(toks[0]);
            if (toks[0][ll - 1] == ':') {
                size_t nl = ll - 1;
                if (nl == 0) { set_err(res, "empty label"); return false; }
                if (nl >= ASM_MAX_LABEL) nl = ASM_MAX_LABEL - 1;
                memcpy(ln->label, toks[0], nl);
                ln->label[nl] = '\0';
                ln->has_label = 1;
                for (int i = 1; i < ntoks; i++) {
                    snprintf(toks[i - 1], ASM_MAX_LABEL, "%s", toks[i]);
                    toks[i][0] = '\0';
                }
                ntoks--;
            }

            if (ntoks > 0) {
                if (!parse_instr(ln, ntoks, toks, res)) return false;
                ln->has_instr = 1;
            }
            nlines++;
        }
    }

    /* Pass 2: fixed-point address/label resolution.
     * Jumps to labels default to short relative form; a target out of the
     * ±31-word range (or whose rel would equal the long-form marker -1)
     * widens the jump to the 32-bit absolute form, then sizes are
     * recomputed until the layout is stable. */
    {
        int changed = 1;
        for (int iter = 0; changed && iter < 32; iter++) {
            changed = 0;
            uint16_t pc = 0;
            for (int i = 0; i < nlines; i++) {
                line_num = lines[i].lineno;
                ln_t *ln = &lines[i];
                ln->addr = pc;
                if (ln->has_label && ln->label[0]) {
                    if (iter == 0) {
                        if (!add_label(ln->label, pc, res)) return false;
                    } else {
                        for (int k = 0; k < nlabels; k++)
                            if (strcmp(labels[k].name, ln->label) == 0)
                                labels[k].addr = pc;
                    }
                }
                int size = ln->has_instr ? (ln->long_flag ? 4 : 2) : 0;
                if (ln->has_instr)
                    ln->next = (uint16_t)(pc + size);
                else
                    ln->next = pc;
                pc = (uint16_t)(pc + size);
            }

            for (int i = 0; i < nlines; i++) {
                line_num = lines[i].lineno;
                ln_t *ln = &lines[i];
                int is_jump = (ln->kind >= LN_JMP && ln->kind <= LN_JLE);
                if (!is_jump || ln->nops < 1) continue;

                opr_t o;
                if (!parse_opr(ln->ops[0], &o)) {
                    set_err(res, "bad jump target"); return false;
                }
                if (o.kind == 2) {
                    uint16_t t;
                    if (!find_label(o.label, &t)) {
                        set_err(res, "unknown label '@%s'", o.label);
                        return false;
                    }
                    ln->target = t;
                    if (!ln->long_flag) {
                        int rel = (int)((int16_t)(t - ln->next)) / 2;
                        if (rel > 31 || rel < -32 || rel == -1) {
                            ln->long_flag = 1;
                            changed = 1;
                        }
                    }
                } else if (o.kind == 1) {
                    /* numeric target = absolute; always long */
                    ln->target = o.imm & 0xFFFF;
                    if (!ln->long_flag) { ln->long_flag = 1; changed = 1; }
                }
            }
        }
    }

    /* Pass 3: emit bytes. */
    for (int i = 0; i < nlines; i++) {
        line_num = lines[i].lineno;
        ln_t *ln = &lines[i];
        if (!ln->has_instr) continue;
        uint32_t ins;
        const char *mn = mnemonics[ln->kind];

        switch (ln->kind) {
            case LN_MOV: {
                opr_t o, s;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "MOV needs a register destination"); return false;
                }
                if (!parse_opr(ln->ops[1], &s)) {
                    set_err(res, "MOV needs a source"); return false;
                }
                if (s.kind == 0) ins = CPUJ_MOV(o.reg, s.reg);
                else {
                    long v = s.kind == 1 ? s.imm : 0;
                    if (s.kind == 2) {
                        uint16_t t;
                        if (!find_label(s.label, &t)) { set_err(res, "unknown label '@%s'", s.label); return false; }
                        v = t;
                    }
                    ins = CPUJ_MOVI(o.reg, (uint16_t)v);
                }
                break;
            }
            case LN_MOVI: {
                opr_t o, s;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "MOVI needs a register"); return false;
                }
                if (!parse_opr(ln->ops[1], &s) || s.kind == 0) {
                    set_err(res, "MOVI expects an immediate"); return false;
                }
                long v = s.kind == 1 ? s.imm : 0;
                if (s.kind == 2) {
                    uint16_t t;
                    if (!find_label(s.label, &t)) { set_err(res, "unknown label '@%s'", s.label); return false; }
                    v = t;
                }
                ins = CPUJ_MOVI(o.reg, (uint16_t)v);
                break;
            }
            case LN_ADD: case LN_SUB: case LN_AND:
            case LN_OR: case LN_XOR: case LN_CMP: {
                int op = OP_ADD;
                if (ln->kind == LN_SUB) op = OP_SUB;
                else if (ln->kind == LN_AND) op = OP_AND;
                else if (ln->kind == LN_OR) op = OP_OR;
                else if (ln->kind == LN_XOR) op = OP_XOR;
                else if (ln->kind == LN_CMP) op = OP_CMP;

                opr_t o, s;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "%s needs a register destination", mn); return false;
                }
                if (!parse_opr(ln->ops[1], &s) || s.kind == 2) {
                    set_err(res, "%s needs a register or immediate source", mn); return false;
                }
                if (s.kind == 0) {
                    switch (op) {
                        case OP_ADD: ins = CPUJ_ADD(o.reg, s.reg); break;
                        case OP_SUB: ins = CPUJ_SUB(o.reg, s.reg); break;
                        case OP_AND: ins = CPUJ_AND(o.reg, s.reg); break;
                        case OP_OR:  ins = CPUJ_OR(o.reg, s.reg); break;
                        case OP_XOR: ins = CPUJ_XOR(o.reg, s.reg); break;
                        default:     ins = CPUJ_CMP(o.reg, s.reg); break;
                    }
                } else {
                    switch (op) {
                        case OP_ADD: ins = CPUJ_ADDI(o.reg, (uint16_t)s.imm); break;
                        case OP_SUB: ins = CPUJ_SUBI(o.reg, (uint16_t)s.imm); break;
                        case OP_AND: ins = CPUJ_ANDI(o.reg, (uint16_t)s.imm); break;
                        case OP_OR:  ins = CPUJ_ORI(o.reg, (uint16_t)s.imm); break;
                        case OP_XOR: ins = CPUJ_XORI(o.reg, (uint16_t)s.imm); break;
                        default:     ins = CPUJ_CMPI(o.reg, (uint16_t)s.imm); break;
                    }
                }
                break;
            }
            case LN_NOT: {
                opr_t o;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "NOT needs a register"); return false;
                }
                ins = CPUJ_NOT(o.reg);
                break;
            }
            case LN_SHL: case LN_SHR: {
                opr_t o, s;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "%s needs a register", mn); return false;
                }
                if (!parse_opr(ln->ops[1], &s) || s.kind != 1 || s.imm < 0 || s.imm > 15) {
                    set_err(res, "shift amount must be 0..15"); return false;
                }
                if (ln->kind == LN_SHL) ins = CPUJ_SHL(o.reg, (int)s.imm);
                else ins = CPUJ_SHR(o.reg, (int)s.imm);
                break;
            }
            case LN_LD: case LN_ST: {
                opr_t o;
                memopr_t m;
                if (ln->kind == LN_LD) {
                    if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                        set_err(res, "LD needs a register destination"); return false;
                    }
                    if (!parse_memopr(ln->ops[1], &m)) {
                        set_err(res, "LD needs [base+off] or [addr]"); return false;
                    }
                    if (m.is_base) {
                        if (m.off < -32 || m.off > 31 || m.off == -1) {
                            set_err(res, "LD offset must be -32..+31 (not -1)"); return false;
                        }
                        ins = CPUJ_LD_OFF(o.reg, m.base, (int)m.off);
                    } else if (m.is_abs_num) {
                        ins = CPUJ_LD_ABS16(o.reg, (uint16_t)m.abs);
                    } else {
                        uint16_t t;
                        if (!find_label(m.label, &t)) { set_err(res, "unknown label '@%s'", m.label); return false; }
                        ins = CPUJ_LD_ABS16(o.reg, t);
                    }
                } else {
                    if (!parse_memopr(ln->ops[0], &m)) {
                        set_err(res, "ST needs [base+off] or [addr]"); return false;
                    }
                    if (!parse_opr(ln->ops[1], &o) || o.kind != 0) {
                        set_err(res, "ST needs a register source"); return false;
                    }
                    if (m.is_base) {
                        if (m.off < -32 || m.off > 31 || m.off == -1) {
                            set_err(res, "ST offset must be -32..+31 (not -1)"); return false;
                        }
                        ins = CPUJ_ST_OFF(m.base, (int)m.off, o.reg);
                    } else if (m.is_abs_num) {
                        ins = CPUJ_ST_ABS16((uint16_t)m.abs, o.reg);
                    } else {
                        uint16_t t;
                        if (!find_label(m.label, &t)) { set_err(res, "unknown label '@%s'", m.label); return false; }
                        ins = CPUJ_ST_ABS16(t, o.reg);
                    }
                }
                break;
            }
            case LN_JMP: case LN_JEQ: case LN_JNE:
            case LN_JGT: case LN_JGE: case LN_JLT: case LN_JLE: {
                int cond = JMP_ALWAYS;
                if (ln->kind == LN_JEQ) cond = JMP_EQ;
                else if (ln->kind == LN_JNE) cond = JMP_NE;
                else if (ln->kind == LN_JGT) cond = JMP_GT;
                else if (ln->kind == LN_JGE) cond = JMP_GE;
                else if (ln->kind == LN_JLT) cond = JMP_LT;
                else if (ln->kind == LN_JLE) cond = JMP_LE;

                if (ln->long_flag) {
                    ins = CPUJ_JB(cond, (uint16_t)ln->target);
                } else {
                    int rel = (int)((int16_t)(ln->target - ln->next)) / 2;
                    switch (cond) {
                        case JMP_ALWAYS: ins = CPUJ_JMP(rel); break;
                        case JMP_EQ: ins = CPUJ_JEQ(rel); break;
                        case JMP_NE: ins = CPUJ_JNE(rel); break;
                        case JMP_GT: ins = CPUJ_JGT(rel); break;
                        case JMP_GE: ins = CPUJ_JGE(rel); break;
                        case JMP_LT: ins = CPUJ_JLT(rel); break;
                        default:     ins = CPUJ_JLE(rel); break;
                    }
                }
                break;
            }
            case LN_CALL: {
                opr_t o;
                if (!parse_opr(ln->ops[0], &o)) {
                    set_err(res, "CALL needs a target"); return false;
                }
                if (o.kind == 0) {
                    ins = CPUJ_CALL_REG(o.reg);   /* register-indirect call */
                    break;
                }
                long v = o.kind == 1 ? o.imm : 0;
                if (o.kind == 2) {
                    uint16_t t;
                    if (!find_label(o.label, &t)) { set_err(res, "unknown label '@%s'", o.label); return false; }
                    v = t;
                }
                ins = CPUJ_CALL_ABS((uint16_t)v);
                break;
            }
            case LN_PUSH: {
                opr_t o;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "PUSH needs a register"); return false;
                }
                ins = CPUJ_PUSH(o.reg);
                break;
            }
            case LN_POP: {
                opr_t o;
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "POP needs a register"); return false;
                }
                ins = CPUJ_POP(o.reg);
                break;
            }
            case LN_RET:  ins = CPUJ_RET();  break;
            case LN_HALT: ins = CPUJ_HALT(); break;
            case LN_NOP:  ins = CPUJ_NOP();  break;
            case LN_TRAP: {
                opr_t o;
                int code = -1;
                if (parse_opr(ln->ops[0], &o) && o.kind == 1) code = (int)o.imm;
                else {
                    const char *nm = ln->ops[0];
                    if (strcasecmp(nm, "PRINT_REG") == 0) code = TRAP_PRINT_REG;
                    else if (strcasecmp(nm, "PRINT_CHAR") == 0) code = TRAP_PRINT_CHAR;
                    else if (strcasecmp(nm, "READ_CHAR") == 0) code = TRAP_READ_CHAR;
                    else if (strcasecmp(nm, "PRINT_STR") == 0) code = TRAP_PRINT_STR;
                    else if (strcasecmp(nm, "HALT") == 0) code = TRAP_HALT;
                }
                if (code < 0 || code > 7) {
                    set_err(res, "unknown trap '%s'", ln->ops[0]); return false;
                }
                ins = CPUJ_TRAP(code);
                break;
            }
            default:
                set_err(res, "internal: bad instruction"); return false;
        }

        if (res->nbytes + 4 > ASM_MAX_CODE) {
            set_err(res, "program too large");
            return false;
        }
        uint16_t w1 = (uint16_t)(ins >> 16);
        res->code[res->nbytes++] = (uint8_t)(w1 >> 8);
        res->code[res->nbytes++] = (uint8_t)(w1 & 0xFF);
        if (CPUJ_IMM(ins) == IMM_LONG) {
            uint16_t w2 = (uint16_t)(ins & 0xFFFF);
            res->code[res->nbytes++] = (uint8_t)(w2 >> 8);
            res->code[res->nbytes++] = (uint8_t)(w2 & 0xFF);
        }
    }

    for (int i = 0; i < nlabels; i++) {
        memcpy(res->labels[i].name, labels[i].name, sizeof res->labels[i].name);
        res->labels[i].addr = labels[i].addr;
    }
    res->nlabels = nlabels;
    return true;
}