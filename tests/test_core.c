#include <stdio.h>
#include <string.h>
#include "cpuj.h"
#include "asm.h"

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
         else { printf("ok:   %s\n", msg); } } while (0)

static void run_prog(cpuj_t *cpu, const asm_result_t *res) {
    cpuj_init(cpu);
    for (int i = 0; i < res->nbytes; i++)
        cpuj_mem_write(cpu, (uint16_t)i, res->code[i]);
    for (int i = 0; !cpu->halted && i < 2000; i++)
        cpuj_tick(cpu);
}

int main(int argc, char **argv) {
    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    (void)verbose;

    /* ── instruction encoding unit tests ── */
    /* short: [op:4][rd:3][rs:3][imm6]; long: imm==0x3F, word2 = imm16 */
    {
        uint32_t a = CPUJ_MOV(1, 2);
        CHECK(CPUJ_OP(a) == OP_MOV && CPUJ_RD(a) == 1 && CPUJ_RS(a) == 2,
              "MOV R1,R2 encoding fields");
        CHECK(!cpuj_is_long(a) && (a >> 16) == 0x0280, "MOV R1,R2 word");
    }
    CHECK((CPUJ_MOVI(3, 0x1234) >> 16) == 0x063F &&
          CPUJ_WIDE(CPUJ_MOVI(3, 0x1234)) == 0x1234 &&
          cpuj_is_long(CPUJ_MOVI(3, 0x1234)), "MOVI R3,#imm16 long form");
    CHECK((CPUJ_ADD(1, 0) >> 16) == 0x2200, "ADD R1,R0 encoding");
    CHECK((CPUJ_ADDI(0, 5) >> 16) == 0x203F && CPUJ_WIDE(CPUJ_ADDI(0, 5)) == 5,
          "ADDI R0,#5 long form");
    CHECK((CPUJ_SUB(2, 1) >> 16) == 0x3440, "SUB R2,R1 encoding");
    CHECK((CPUJ_JEQ(4) >> 16) == 0xD044, "JEQ +4 encoding");
    CHECK((CPUJ_JMP(4) >> 16) == 0xD004, "JMP +4 encoding");
    CHECK((CPUJ_JMP(-4) >> 16) == 0xD03C, "JMP -4 encoding");
    CHECK((CPUJ_JMP_ABS(0x100) >> 16) == 0xD03F && CPUJ_WIDE(CPUJ_JMP_ABS(0x100)) == 0x100,
          "JMP abs16 long form");
    CHECK((CPUJ_JEQ_ABS(0x100) >> 16) == 0xD23F, "JEQ abs16 carries condition");
    CHECK((CPUJ_PUSH(2) >> 16) == 0xE400, "PUSH R2");
    CHECK((CPUJ_POP(1) >> 16) == 0xE201, "POP R1");
    CHECK((CPUJ_TRAP(0) >> 16) == 0xE004, "TRAP PRINT_REG");
    CHECK((CPUJ_RET() >> 16) == 0xE003, "RET");
    CHECK((CPUJ_HALT() >> 16) == 0xE005, "HALT");
    CHECK((CPUJ_NOP() >> 16) == 0xE006, "NOP");
    CHECK((CPUJ_CALL_ABS(0x50) >> 16) == 0xE03F && CPUJ_WIDE(CPUJ_CALL_ABS(0x50)) == 0x50,
          "CALL abs16 long form");
    CHECK((CPUJ_LD_ABS16(0, 0x200) >> 16) == 0xB03F, "LD [abs16] long form");
    CHECK((CPUJ_ST_OFF(1, 4, 2) >> 16) == 0xC444, "ST [R1+4], R2");

    /* ── ALU flag + value tests (16-bit) ── */
    {
        cpuj_t cpu; cpuj_init(&cpu);
        cpu.r[0] = 0x8000;
        uint16_t r = cpuj_alu(&cpu, OP_ADD, cpu.r[0], 0x8000);
        CHECK(r == 0x0000, "ADD 0x8000+0x8000 == 0");
        CHECK((cpu.flags & CPUJ_FLAG_C) != 0, "ADD 0x8000+0x8000 Carry");
        CHECK((cpu.flags & CPUJ_FLAG_V) != 0, "ADD 0x8000+0x8000 Overflow");
        CHECK((cpu.flags & CPUJ_FLAG_N) == 0, "ADD 0x8000+0x8000 not Negative");
    }
    {
        cpuj_t cpu; cpuj_init(&cpu);
        uint16_t r = cpuj_alu(&cpu, OP_ADD, 0x7FFF, 1);
        CHECK(r == 0x8000, "ADD 0x7FFF+1 == 0x8000");
        CHECK((cpu.flags & CPUJ_FLAG_V) != 0, "ADD 0x7FFF+1 Overflow");
        CHECK((cpu.flags & CPUJ_FLAG_N) != 0, "ADD 0x7FFF+1 Negative");
    }
    {
        cpuj_t cpu; cpuj_init(&cpu);
        uint16_t r = cpuj_alu(&cpu, OP_SUB, 5, 9);
        CHECK(r == 0xFFFC, "SUB 5-9 wraps to 0xFFFC");
        CHECK((cpu.flags & CPUJ_FLAG_C) != 0, "SUB 5-9 Carry (borrow)");
        CHECK((cpu.flags & CPUJ_FLAG_N) != 0, "SUB 5-9 Negative");
    }
    {
        cpuj_t cpu; cpuj_init(&cpu);
        uint16_t r = cpuj_alu(&cpu, OP_CMP, 10, 10);
        CHECK((cpu.flags & CPUJ_FLAG_Z) != 0, "CMP equal sets Zero");
        (void)r;
    }

    /* ── stack tests ── */
    {
        cpuj_t cpu; cpuj_init(&cpu);
        cpuj_push16(&cpu, 0x1234);
        cpuj_push16(&cpu, 0xABCD);
        CHECK(cpu.sp == 0xFFFB, "SP decremented twice (0xFFFF -> 0xFFFB)");
        CHECK(cpuj_pop16(&cpu) == 0xABCD, "LIFO pop (1)");
        CHECK(cpuj_pop16(&cpu) == 0x1234, "LIFO pop (2)");
        CHECK(cpu.sp == 0xFFFF, "SP restored after pops");
    }

    /* ── assembler: countdown 3..0 with JGE (JGT on flags) ── */
    {
        const char *src =
            "; countdown 3..0\n"
            "    MOVI R0, #3\n"
            "loop:\n"
            "    MOV R1, R0\n"
            "    SUB R0, #1\n"
            "    JGE @loop\n"
            "    HALT\n";
        asm_result_t res;
        CHECK(asm_assemble(src, &res), "assembly ok");
        if (!res.error[0]) {
            CHECK(res.labels[0].addr == 4 && strcmp(res.labels[0].name, "loop") == 0,
                  "label 'loop' at address 4");
            CHECK(res.nbytes == 14, "MOVI+MOV+SUB+JGE+HALT = 14 bytes");
            /* MOVI R0,#3 at 0 (long), MOV R1,R0 at 4, SUB long at 6,
               JGE rel short at 10, HALT at 12 */
            CHECK(res.code[0] == 0x00 && res.code[1] == 0x3F &&
                  res.code[2] == 0x00 && res.code[3] == 0x03,
                  "first instr is MOVI R0,#3 (long form)");
            cpuj_t cpu;
            run_prog(&cpu, &res);
            CHECK(cpu.r[1] == 0, "loop captures 0 on final pass");
            CHECK((cpu.flags & CPUJ_FLAG_N) != 0, "last SUB left Negative set");
        }
    }

    /* ── run: MOVI + register ALU ── */
    {
        const char *src =
            "    MOVI R0, #5\n"
            "    MOVI R1, #3\n"
            "    ADD  R0, R1\n"
            "    SUB  R2, #2\n"
            "    XOR  R0, R2\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[0] == (8 ^ 0xFFFE), "8 ^ 0xFFFE = 0xFFF6");
            CHECK(cpu.halted, "program halts cleanly");
        } else { CHECK(false, "assemble MOVI/ALU program"); }
    }

    /* ── run: stack swap + add ── */
    {
        const char *src =
            "    MOVI R0, #5\n"
            "    MOVI R1, #3\n"
            "    PUSH R0\n"
            "    PUSH R1\n"
            "    POP  R0\n"
            "    POP  R1\n"
            "    ADD  R0, R1\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[0] == 8, "stack swap + add gives R0 == 8");
            CHECK(cpu.r[1] == 5, "R1 restored to 5 after swap");
            CHECK(cpu.sp == 0xFFFF, "stack balanced after swap");
        } else { CHECK(false, "assemble swap program"); }
    }

    /* ── run: CALL/RET subroutine ── */
    {
        const char *src =
            "    MOVI R1, #3\n"
            "    CALL @inc2\n"
            "    HALT\n"
            "inc2:\n"
            "    ADD R1, #2\n"
            "    RET\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[1] == 5, "CALL/RET subroutine leaves R1 == 5");
            CHECK(cpu.sp == 0xFFFF, "stack balanced after CALL/RET");
        } else { CHECK(false, "assemble CALL/RET program"); }
    }

    /* ── run: pointer LD/ST loop ── */
    {
        const char *src =
            "    MOVI R1, #0x40\n"
            "    MOVI R2, #3\n"
            "loop:\n"
            "    ST [R1], R2\n"
            "    ADD R1, #1\n"
            "    SUB R2, #1\n"
            "    JGT @loop\n"
            "    MOVI R1, #0x40\n"
            "    LD R0, [R1]\n"
            "    ADD R1, #1\n"
            "    LD R2, [R1]\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.ram[0x40] == 3, "mem[0x40] == 3");
            CHECK(cpu.ram[0x41] == 2, "mem[0x41] == 2");
            CHECK(cpu.ram[0x42] == 1, "mem[0x42] == 1");
            CHECK(cpu.r[0] == 3, "LD [R1] reloaded 3");
            CHECK(cpu.r[2] == 2, "LD [R1+1] gave 2");
        } else { CHECK(false, "assemble pointer LD/ST loop"); }
    }

    /* ── run: absolute LD/ST ── */
    {
        const char *src =
            "    MOVI R0, #0x2A\n"
            "    ST [0x200], R0\n"
            "    MOVI R1, #0\n"
            "    LD R1, [0x200]\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.ram[0x200] == 0x2A, "abs ST stored byte");
            CHECK(cpu.r[1] == 0x2A, "abs LD loaded back");
        } else { CHECK(false, "assemble absolute LD/ST"); }
    }

    /* ── run: shifts and logic ── */
    {
        const char *src =
            "    MOVI R0, #1\n"
            "    SHL R0, #4       ; 16\n"
            "    SHL R0, #1       ; 32\n"
            "    SHR R0, #2       ; 8\n"
            "    MOVI R1, #0x0F0F\n"
            "    XOR R0, R1       ; 0x0F07\n"
            "    MOVI R2, #0xFF\n"
            "    AND R0, R2       ; 0x07\n"
            "    NOT R0           ; 0xFFF8\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[0] == 0xFFF8, "shifts + logic produce 0xFFF8");
        } else { CHECK(false, "assemble shifts/logic program"); }
    }

    /* ── run: conditional JEQ after CMP ── */
    {
        const char *src =
            "    MOVI R0, #5\n"
            "    CMP R0, R0\n"
            "    JEQ @yes\n"
            "    MOVI R1, #99\n"
            "yes:\n"
            "    MOVI R2, #1\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[1] == 0, "JEQ taken; skipped MOVI R1");
            CHECK(cpu.r[2] == 1, "reached target label");
        } else { CHECK(false, "assemble JEQ program"); }
    }

    /* ── run: far jump widens to 32-bit absolute ── */
    {
        char src[1024];
        int off = sprintf(src, "    JMP @theend\n");
        for (int i = 0; i < 40; i++) off += sprintf(src + off, "    NOP\n");
        off += sprintf(src + off, "theend:\n    MOVI R1, #7\n    HALT\n");
        (void)off;
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            CHECK(res.code[0] == 0xD0 && res.code[1] == 0x3F &&
                  res.code[2] == 0x00 && res.code[3] == 0x54,
                  "JMP widened to long form, target 0x54");
            CHECK(res.nbytes == 90, "layout: 4 + 40 NOPs + MOVI + HALT = 90");
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[1] == 7, "far jump lands on target");
        } else { CHECK(false, "assemble far-jump program"); }
    }

    /* ── run: PRINT_STR traps emit expected bytes ── */
    {
        const char *src =
            "    MOVI R1, #0x100\n"
            "    MOVI R0, #65    ; 'A'\n"
            "    ST [R1], R0\n"
            "    ADD R1, #1\n"
            "    MOVI R0, #66    ; 'B'\n"
            "    ST [R1], R0\n"
            "    ADD R1, #1\n"
            "    MOVI R0, #0\n"
            "    ST [R1], R0\n"
            "    MOVI R0, #0x100\n"
            "    TRAP PRINT_STR\n"
            "    HALT\n";
        asm_result_t res;
        if (!asm_assemble(src, &res)) {
            printf("FAIL: PRINT_STR assemble: %s\n", res.error);
            failures++;
        } else {
            cpuj_t cpu; cpuj_init(&cpu);
            for (int i = 0; i < res.nbytes; i++)
                cpuj_mem_write(&cpu, (uint16_t)i, res.code[i]);
            FILE *cap = tmpfile();
            FILE *old = stdout;
            stdout = cap;
            for (int i = 0; !cpu.halted && i < 2000; i++)
                cpuj_tick(&cpu);
            stdout = old;
            fflush(cap);
            rewind(cap);
            char out[64];
            if (!fgets(out, sizeof out, cap)) out[0] = '\0';
            fclose(cap);
            CHECK(strcmp(out, "AB") == 0, "PRINT_STR printed 'AB'");
        }
    }

    /* ── run: number literal formats ── */
    {
        const char *src =
            "    MOVI R0, #0x0F   ; hex\n"
            "    MOVI R1, #0b1010 ; binary\n"
            "    MOVI R2, #010    ; octal = 8\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[0] == 15, "hex literal 0x0F");
            CHECK(cpu.r[1] == 10, "binary literal 0b1010");
            CHECK(cpu.r[2] == 8,  "octal literal 010");
        } else { CHECK(false, "assemble literal formats"); }
    }

    /* ── run: 32-bit general registers hold full 16-bit values ── */
    {
        const char *src =
            "    MOVI R3, #0x8000\n"
            "    MOV R4, R3\n"
            "    ADD R3, R3        ; 0x10000 -> 0x0000\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu; run_prog(&cpu, &res);
            CHECK(cpu.r[4] == 0x8000, "MOV preserves 0x8000");
            CHECK(cpu.r[3] == 0x0000, "0x8000+0x8000 wraps to 0");
            CHECK((cpu.flags & CPUJ_FLAG_C) != 0, "carry out of 16-bit add");
        } else { CHECK(false, "assemble 16-bit addition program"); }
    }

    /* ── assembler error handling ── */
    {
        asm_result_t res;
        CHECK(!asm_assemble("    BOGUS R0\n", &res), "unknown mnemonic rejected");
        CHECK(!asm_assemble("    MOVI R0, #1\n    JMP @missing\n", &res),
              "unknown label rejected");
        CHECK(!asm_assemble("    ST [R1-1], R0\n", &res), "offset -1 rejected");
        CHECK(!asm_assemble("    SHL R0, #16\n", &res), "shift 16 rejected");
        CHECK(!asm_assemble("    LD R0, [R1+100]\n", &res), "LD offset too large rejected");
    }

    /* ── disassembler sanity ── */
    {
        char buf[80];
        cpuj_disassemble(CPUJ_MOVI(3, 0x1234), buf, sizeof buf);
        CHECK(strstr(buf, "MOVI") != NULL, "disassembler prints MOVI");
        cpuj_disassemble(CPUJ_JEQ(-4), buf, sizeof buf);
        CHECK(strstr(buf, "JEQ") != NULL, "disassembler prints JEQ");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}