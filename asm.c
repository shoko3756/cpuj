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
 * Accepts any 32-bit quantity (INT32_MIN..UINT32_MAX). */
static bool parse_num(const char *tok, int64_t *out) {
    while (*tok == ' ' || *tok == '\t') tok++;
    if (*tok == '#') tok++;
    if (*tok == '\0') return false;

    const char *p = tok;
    int64_t sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') p++;

    char *end = NULL;
    long long v;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        v = strtoll(p + 2, &end, 16);
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))
        v = strtoll(p + 2, &end, 2);
    else if (p[0] == '0' && p[1] != '\0')
        v = strtoll(p, &end, 8);
    else
        v = strtoll(p, &end, 10);

    if (end == p || *end != '\0') return false;
    v *= sign;
    if (v < INT32_MIN || v > UINT32_MAX) return false;
    *out = (int64_t)v;
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
    int64_t imm;
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

/* Memory operand "[Rbase+off]" (a register base is required; there is
 * no absolute-addressing form — load the base with MOVI/MOVU first). */
typedef struct {
    int  base;
    int64_t off;
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

    char comp[16];
    size_t clen = 0;
    const char *q = buf;
    while (*q && !isspace((unsigned char)*q) && *q != '+' && *q != '-'
           && clen < sizeof comp - 1)
        comp[clen++] = *q++;
    comp[clen] = '\0';
    int base;
    if (!is_reg(comp, &base)) return false;
    m->base = base;
    m->off = 0;
    while (isspace((unsigned char)*q)) q++;
    if (*q == '\0') return true;
    if (!parse_num(q, &m->off)) return false;
    return true;
}

/* ── Instruction lines ─────────────────────────────────────────────── */

typedef enum {
    LN_MOV, LN_MOVI, LN_MOVU,
    LN_ADD, LN_ADDI, LN_SUB, LN_SUBI, LN_AND, LN_ANDI,
    LN_OR, LN_ORI, LN_XOR, LN_XORI, LN_CMP, LN_CMPI,
    LN_NOT, LN_SHL, LN_SHLI, LN_SHR, LN_SHRI,
    LN_LD, LN_ST, LN_LDB, LN_STB,
    LN_JMP, LN_JEQ, LN_JNE, LN_JGT, LN_JGE, LN_JLT, LN_JLE,
    LN_JMPR, LN_CALL, LN_CALLR,
    LN_PUSH, LN_POP, LN_RET, LN_TRAP, LN_HALT, LN_NOP
} ln_kind_t;

typedef struct {
    int       has_label;
    char      label[ASM_MAX_LABEL];
    int       has_instr;    /* line contains an instruction (vs label-only) */
    ln_kind_t kind;
    char      ops[MAXTOKS][ASM_MAX_LABEL];
    int       nops;
    int       lineno;
    uint32_t  target;       /* resolved absolute target (for branches) */
    uint32_t  addr;         /* byte address of instruction start */
    uint32_t  next;         /* byte address after the instruction */
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

static bool find_label(const char *name, uint32_t *addr) {
    for (int i = 0; i < nlabels; i++)
        if (strcmp(labels[i].name, name) == 0) { *addr = labels[i].addr; return true; }
    return false;
}

static bool add_label(const char *name, uint32_t addr, asm_result_t *res) {
    uint32_t tmp;
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

#define LN_TAB(x) { #x, LN_##x }

static bool kind_from_name(const char *mn, ln_kind_t *k) {
    struct { const char *name; ln_kind_t k; } tab[] = {
        LN_TAB(MOV), LN_TAB(MOVI), LN_TAB(MOVU),
        LN_TAB(ADD), LN_TAB(ADDI), LN_TAB(SUB), LN_TAB(SUBI),
        LN_TAB(AND), LN_TAB(ANDI), LN_TAB(OR), LN_TAB(ORI),
        LN_TAB(XOR), LN_TAB(XORI), LN_TAB(CMP), LN_TAB(CMPI),
        LN_TAB(NOT), LN_TAB(SHL), LN_TAB(SHLI), LN_TAB(SHR), LN_TAB(SHRI),
        LN_TAB(LD), LN_TAB(ST), LN_TAB(LDB), LN_TAB(STB),
        LN_TAB(JMP), LN_TAB(JEQ), LN_TAB(JNE), LN_TAB(JGT),
        LN_TAB(JGE), LN_TAB(JLT), LN_TAB(JLE),
        LN_TAB(JMPR), LN_TAB(CALL), LN_TAB(CALLR),
        LN_TAB(PUSH), LN_TAB(POP), LN_TAB(RET), LN_TAB(TRAP),
        LN_TAB(HALT), LN_TAB(NOP),
    };
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcasecmp(mn, tab[i].name) == 0) { *k = tab[i].k; return true; }
    return false;
}

static const char *mnemonics[LN_NOP + 1] = {
    [LN_MOV] = "MOV", [LN_MOVI] = "MOVI", [LN_MOVU] = "MOVU",
    [LN_ADD] = "ADD", [LN_ADDI] = "ADDI", [LN_SUB] = "SUB",
    [LN_SUBI] = "SUBI", [LN_AND] = "AND", [LN_ANDI] = "ANDI",
    [LN_OR] = "OR", [LN_ORI] = "ORI", [LN_XOR] = "XOR",
    [LN_XORI] = "XORI", [LN_CMP] = "CMP", [LN_CMPI] = "CMPI",
    [LN_NOT] = "NOT", [LN_SHL] = "SHL", [LN_SHLI] = "SHLI",
    [LN_SHR] = "SHR", [LN_SHRI] = "SHRI",
    [LN_LD] = "LD", [LN_ST] = "ST", [LN_LDB] = "LDB", [LN_STB] = "STB",
    [LN_JMP] = "JMP", [LN_JEQ] = "JEQ", [LN_JNE] = "JNE",
    [LN_JGT] = "JGT", [LN_JGE] = "JGE", [LN_JLT] = "JLT",
    [LN_JLE] = "JLE", [LN_JMPR] = "JMPR", [LN_CALL] = "CALL",
    [LN_CALLR] = "CALLR", [LN_PUSH] = "PUSH", [LN_POP] = "POP",
    [LN_RET] = "RET", [LN_TRAP] = "TRAP", [LN_HALT] = "HALT",
    [LN_NOP] = "NOP",
};

static int operand_need(ln_kind_t k) {
    switch (k) {
        case LN_MOV: case LN_MOVI: case LN_MOVU:
        case LN_ADD: case LN_ADDI: case LN_SUB: case LN_SUBI:
        case LN_AND: case LN_ANDI: case LN_OR: case LN_ORI:
        case LN_XOR: case LN_XORI: case LN_CMP: case LN_CMPI:
        case LN_SHL: case LN_SHLI: case LN_SHR: case LN_SHRI:
        case LN_LD: case LN_ST: case LN_LDB: case LN_STB:
            return 2;
        case LN_NOT: case LN_JMP: case LN_JEQ: case LN_JNE:
        case LN_JGT: case LN_JGE: case LN_JLT: case LN_JLE:
        case LN_JMPR: case LN_CALL: case LN_CALLR:
        case LN_PUSH: case LN_POP: case LN_TRAP:
            return 1;
        case LN_RET: case LN_HALT: case LN_NOP:
            return 0;
    }
    return -1;
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
    return true;
}

/* Resolve a branch target operand to an absolute address. */
static bool branch_target(const ln_t *ln, uint32_t *out, asm_result_t *res) {
    opr_t o;
    if (!parse_opr(ln->ops[0], &o)) {
        set_err(res, "bad branch target");
        return false;
    }
    if (o.kind == 0) {
        set_err(res, "%s expects a label or address, not a register "
                     "(use JMPR for register jumps)", mnemonics[ln->kind]);
        return false;
    }
    if (o.kind == 1) {
        *out = (uint32_t)o.imm;
        return true;
    }
    uint32_t t;
    if (!find_label(o.label, &t)) {
        set_err(res, "unknown label '@%s'", o.label);
        return false;
    }
    *out = t;
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

    /* Pass 2: fixed layout (every instruction is 4 bytes) + labels. */
    {
        uint32_t pc = 0;
        for (int i = 0; i < nlines; i++) {
            line_num = lines[i].lineno;
            ln_t *ln = &lines[i];
            ln->addr = pc;
            if (ln->has_label && ln->label[0]) {
                if (!add_label(ln->label, pc, res)) return false;
            }
            ln->next = pc + (ln->has_instr ? 4 : 0);
            pc = ln->next;
        }

        /* Resolve branch targets and check the ±20-bit range. */
        for (int i = 0; i < nlines; i++) {
            line_num = lines[i].lineno;
            ln_t *ln = &lines[i];
            int is_branch = (ln->kind >= LN_JMP && ln->kind <= LN_JLE) ||
                            ln->kind == LN_CALL;
            if (!is_branch || ln->nops < 1) continue;
            if (!branch_target(ln, &ln->target, res)) return false;
            int64_t rel = (int64_t)ln->target - (int64_t)ln->next;
            if (rel < -524288 || rel > 524287) {
                set_err(res, "%s target out of ±0x80000-byte range",
                        mnemonics[ln->kind]);
                return false;
            }
        }
    }

    /* Pass 3: emit bytes. */
    for (int i = 0; i < nlines; i++) {
        line_num = lines[i].lineno;
        ln_t *ln = &lines[i];
        if (!ln->has_instr) continue;
        uint32_t ins = 0;
        const char *mn = mnemonics[ln->kind];

        opr_t o, s;
        memopr_t m;

        switch (ln->kind) {
            case LN_MOV:
            case LN_MOVI: {
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "%s needs a register destination", mn); return false;
                }
                if (!parse_opr(ln->ops[1], &s)) {
                    set_err(res, "%s needs a source", mn); return false;
                }
                if (ln->kind == LN_MOV && s.kind == 0) {
                    ins = CPUJ_MOV(o.reg, s.reg);
                } else {
                    int64_t v;
                    if (s.kind == 2) {
                        uint32_t t;
                        if (!find_label(s.label, &t)) { set_err(res, "unknown label '@%s'", s.label); return false; }
                        v = t;
                    } else if (s.kind == 1) {
                        v = s.imm;
                    } else {
                        set_err(res, "MOVI expects an immediate or label"); return false;
                    }
                    if (v < 0 || v > 0xFFFFF) {
                        set_err(res, "%s immediate must be 0..0xFFFFF", mn); return false;
                    }
                    ins = CPUJ_MOVI(o.reg, (uint32_t)v);
                }
                break;
            }

            case LN_MOVU: {
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "MOVU needs a register destination"); return false;
                }
                if (!parse_opr(ln->ops[1], &s) || s.kind != 1) {
                    set_err(res, "MOVU expects an immediate"); return false;
                }
                if (s.imm < 0 || s.imm > 0xFFF) {
                    set_err(res, "MOVU value must be 0..0xFFF"); return false;
                }
                ins = CPUJ_MOVU(o.reg, (uint32_t)s.imm);
                break;
            }

            case LN_ADD: case LN_ADDI: case LN_SUB: case LN_SUBI:
            case LN_AND: case LN_ANDI: case LN_OR: case LN_ORI:
            case LN_XOR: case LN_XORI: case LN_CMP: case LN_CMPI: {
                int op = OP_ADD;
                int isi = 0;
                switch (ln->kind) {
                    case LN_ADD:  op = OP_ADD; break;
                    case LN_ADDI: op = OP_ADD; isi = 1; break;
                    case LN_SUB:  op = OP_SUB; break;
                    case LN_SUBI: op = OP_SUB; isi = 1; break;
                    case LN_AND:  op = OP_AND; break;
                    case LN_ANDI: op = OP_AND; isi = 1; break;
                    case LN_OR:   op = OP_OR; break;
                    case LN_ORI:  op = OP_OR; isi = 1; break;
                    case LN_XOR:  op = OP_XOR; break;
                    case LN_XORI: op = OP_XOR; isi = 1; break;
                    case LN_CMP:  op = OP_CMP; break;
                    default:      op = OP_CMP; isi = 1; break;
                }
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "%s needs a register destination", mn); return false;
                }
                if (!parse_opr(ln->ops[1], &s)) {
                    set_err(res, "%s needs a source", mn); return false;
                }
                if (s.kind == 1) {
                    if (s.imm < 0 || s.imm > 0xFFFFF) {
                        set_err(res, "%s immediate must be 0..0xFFFFF", mn); return false;
                    }
                    if (op == OP_ADD) ins = CPUJ_ADDI(o.reg, (uint32_t)s.imm);
                    else if (op == OP_SUB) ins = CPUJ_SUBI(o.reg, (uint32_t)s.imm);
                    else if (op == OP_AND) ins = CPUJ_ANDI(o.reg, (uint32_t)s.imm);
                    else if (op == OP_OR) ins = CPUJ_ORI(o.reg, (uint32_t)s.imm);
                    else if (op == OP_XOR) ins = CPUJ_XORI(o.reg, (uint32_t)s.imm);
                    else ins = CPUJ_CMPI(o.reg, (uint32_t)s.imm);
                } else if (s.kind == 0) {
                    if (isi) {
                        set_err(res, "%s expects an immediate (use %s for registers)",
                                mn, mnemonics[ln->kind - 1]); return false;
                    }
                    if (op == OP_ADD) ins = CPUJ_ADD(o.reg, s.reg);
                    else if (op == OP_SUB) ins = CPUJ_SUB(o.reg, s.reg);
                    else if (op == OP_AND) ins = CPUJ_AND(o.reg, s.reg);
                    else if (op == OP_OR) ins = CPUJ_OR(o.reg, s.reg);
                    else if (op == OP_XOR) ins = CPUJ_XOR(o.reg, s.reg);
                    else ins = CPUJ_CMP(o.reg, s.reg);
                } else {
                    set_err(res, "%s needs a register or immediate source", mn); return false;
                }
                break;
            }

            case LN_NOT: {
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "NOT needs a register"); return false;
                }
                ins = CPUJ_NOT(o.reg);
                break;
            }

            case LN_SHL: case LN_SHLI: case LN_SHR: case LN_SHRI: {
                int isi = (ln->kind == LN_SHLI || ln->kind == LN_SHRI);
                int isl = (ln->kind == LN_SHL || ln->kind == LN_SHLI);
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "%s needs a register", mn); return false;
                }
                if (!parse_opr(ln->ops[1], &s)) {
                    set_err(res, "%s needs a count", mn); return false;
                }
                if (s.kind == 1) {
                    if (s.imm < 0 || s.imm > 31) {
                        set_err(res, "shift amount must be 0..31"); return false;
                    }
                    if (isl) ins = CPUJ_SHLI(o.reg, (uint32_t)s.imm);
                    else ins = CPUJ_SHRI(o.reg, (uint32_t)s.imm);
                } else if (s.kind == 0) {
                    if (isi) {
                        set_err(res, "%s expects an immediate count", mn); return false;
                    }
                    if (isl) ins = CPUJ_SHL(o.reg, s.reg);
                    else ins = CPUJ_SHR(o.reg, s.reg);
                } else {
                    set_err(res, "%s needs a count", mn); return false;
                }
                break;
            }

            case LN_LD: case LN_ST: case LN_LDB: case LN_STB: {
                int is_load = (ln->kind == LN_LD || ln->kind == LN_LDB);
                int is_byte = (ln->kind == LN_LDB || ln->kind == LN_STB);
                int mi = is_load ? 1 : 0;
                if (!parse_memopr(ln->ops[mi], &m)) {
                    set_err(res, "%s needs a [Rbase+off] operand (no absolute form)",
                            mn); return false;
                }
                if (m.off < -524288 || m.off > 524287) {
                    set_err(res, "%s offset must be -0x80000..+0x7FFFF", mn); return false;
                }
                if (is_load) {
                    if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                        set_err(res, "%s needs a register destination", mn); return false;
                    }
                    ins = is_byte ? CPUJ_LDB(o.reg, m.base, (int32_t)m.off)
                                  : CPUJ_LD(o.reg, m.base, (int32_t)m.off);
                } else {
                    if (!parse_opr(ln->ops[1], &o) || o.kind != 0) {
                        set_err(res, "%s needs a register source", mn); return false;
                    }
                    ins = is_byte ? CPUJ_STB(m.base, (int32_t)m.off, o.reg)
                                  : CPUJ_ST(m.base, (int32_t)m.off, o.reg);
                }
                break;
            }

            case LN_JMP: case LN_JEQ: case LN_JNE:
            case LN_JGT: case LN_JGE: case LN_JLT: case LN_JLE: {
                int64_t rel = (int64_t)ln->target - (int64_t)ln->next;
                int op = OP_JMP + (ln->kind - LN_JMP);
                ins = CPUJ_BR(op, (int32_t)rel);
                break;
            }

            case LN_JMPR: {
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "JMPR needs a register"); return false;
                }
                ins = CPUJ_JMPR(o.reg);
                break;
            }

            case LN_CALL: {
                int64_t rel = (int64_t)ln->target - (int64_t)ln->next;
                ins = CPUJ_CALL((int32_t)rel);
                break;
            }

            case LN_CALLR: {
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "CALLR needs a register"); return false;
                }
                ins = CPUJ_CALLR(o.reg);
                break;
            }

            case LN_PUSH: {
                if (!parse_opr(ln->ops[0], &o) || o.kind != 0) {
                    set_err(res, "PUSH needs a register"); return false;
                }
                ins = CPUJ_PUSH(o.reg);
                break;
            }

            case LN_POP: {
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
        res->code[res->nbytes]     = (uint8_t)(ins >> 24);
        res->code[res->nbytes + 1] = (uint8_t)(ins >> 16);
        res->code[res->nbytes + 2] = (uint8_t)(ins >> 8);
        res->code[res->nbytes + 3] = (uint8_t)ins;
        res->nbytes += 4;
    }

    for (int i = 0; i < nlabels; i++) {
        memcpy(res->labels[i].name, labels[i].name, sizeof res->labels[i].name);
        res->labels[i].addr = labels[i].addr;
    }
    res->nlabels = nlabels;
    return true;
}