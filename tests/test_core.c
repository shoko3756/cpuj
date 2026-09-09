#include <stdio.h>
#include <string.h>
#include "cpuj.h"
#include "asm.h"

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
         else { printf("ok:   %s\n", msg); } } while (0)

static bool run_prog(cpuj_t *cpu, const asm_result_t *res) {
    if (!cpuj_init(cpu)) return false;
    for (int i = 0; i < res->nbytes; i++)
        cpuj_mem_write(cpu, (uint32_t)i, res->code[i]);
    for (int i = 0; !cpu->halted && i < 2000; i++)
        cpuj_tick(cpu);
    return true;
}

int main(int argc, char **argv) {
    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    (void)verbose;

    /* ── instruction encoding unit tests ── */
    /* fixed 32-bit: [op:6][rd:3][rs:3][imm:20], big-endian in RAM */
    {
        uint32_t a = CPUJ_MOV(1, 2);
        CHECK(CPUJ_OP(a) == OP_MOV && CPUJ_RD(a) == 1 && CPUJ_RS(a) == 2,
              "MOV R1,R2 encoding fields");
        CHECK((a >> 24) == 0x00 && a == 0x00A00000, "MOV R1,R2 word");
    }
    CHECK(CPUJ_OP(CPUJ_MOVI(3, 0x12345)) == OP_MOVI &&
          CPUJ_RD(CPUJ_MOVI(3, 0x12345)) == 3 &&
          CPUJ_IMM(CPUJ_MOVI(3, 0x12345)) == 0x12345,
          "MOVI R3,#0x12345 encoding");
    CHECK(CPUJ_OP(CPUJ_ADDI(0, 5)) == OP_ADDI && CPUJ_IMM(CPUJ_ADDI(0, 5)) == 5,
          "ADDI R0,#5 encoding");
    CHECK(CPUJ_OP(CPUJ_ADD(2, 1)) == OP_ADD && CPUJ_RD(CPUJ_ADD(2, 1)) == 2 &&
          CPUJ_RS(CPUJ_ADD(2, 1)) == 1, "ADD R2,R1 encoding");
    CHECK(CPUJ_OP(CPUJ_JEQ(4)) == OP_JEQ && CPUJ_IMM(CPUJ_JEQ(4)) == 4,
          "JEQ +4 encoding");
    CHECK(CPUJ_SIMM(CPUJ_JMP(-4)) == -4, "JMP -4 sign-extends");
    CHECK(CPUJ_OP(CPUJ_MOVU(0, 0x123)) == OP_MOVU && CPUJ_IMM(CPUJ_MOVU(0, 0x123)) == 0x123,
          "MOVU R0,#0x123 encoding");
    CHECK(CPUJ_OP(CPUJ_PUSH(2)) == OP_PUSH && CPUJ_RD(CPUJ_PUSH(2)) == 2, "PUSH R2");
    CHECK(CPUJ_OP(CPUJ_POP(1)) == OP_POP && CPUJ_RD(CPUJ_POP(1)) == 1, "POP R1");
    CHECK(CPUJ_OP(CPUJ_TRAP(0)) == OP_TRAP && CPUJ_RD(CPUJ_TRAP(0)) == 0,
          "TRAP PRINT_REG");
    CHECK(CPUJ_OP(CPUJ_RET()) == OP_RET, "RET");
    CHECK(CPUJ_OP(CPUJ_HALT()) == OP_HALT, "HALT");
    CHECK(CPUJ_OP(CPUJ_NOP()) == OP_NOP, "NOP");
    CHECK(CPUJ_OP(CPUJ_LD(0, 1, 4)) == OP_LD && CPUJ_RS(CPUJ_LD(0, 1, 4)) == 1 &&
          CPUJ_IMM(CPUJ_LD(0, 1, 4)) == 4, "LD R0,[R1+4] encoding");
    CHECK(CPUJ_OP(CPUJ_STB(1, -1, 2)) == OP_STB && CPUJ_SIMM(CPUJ_STB(1, -1, 2)) == -1,
          "STB [R1-1], R2 encodes -1");

    /* ── ALU flag + value tests (32-bit) ── */
    {
        cpuj_t cpu; if (!cpuj_init(&cpu)) return 1;
        uint32_t r = cpuj_alu(&cpu, OP_ADD, 0x80000000, 0x80000000);
        CHECK(r == 0x00000000, "ADD 0x80000000+0x80000000 == 0");
        CHECK((cpu.flags & CPUJ_FLAG_C) != 0, "ADD ... Carry");
        CHECK((cpu.flags & CPUJ_FLAG_V) != 0, "ADD ... Overflow");
        CHECK((cpu.flags & CPUJ_FLAG_N) == 0, "ADD ... not Negative");
        cpuj_free(&cpu);
    }
    {
        cpuj_t cpu; if (!cpuj_init(&cpu)) return 1;
        uint32_t r = cpuj_alu(&cpu, OP_ADD, 0x7FFFFFFF, 1);
        CHECK(r == 0x80000000, "ADD 0x7FFFFFFF+1 == 0x80000000");
        CHECK((cpu.flags & CPUJ_FLAG_V) != 0, "ADD 0x7FFFFFFF+1 Overflow");
        CHECK((cpu.flags & CPUJ_FLAG_N) != 0, "ADD 0x7FFFFFFF+1 Negative");
        cpuj_free(&cpu);
    }
    {
        cpuj_t cpu; if (!cpuj_init(&cpu)) return 1;
        uint32_t r = cpuj_alu(&cpu, OP_SUB, 5, 9);
        CHECK(r == 0xFFFFFFFC, "SUB 5-9 wraps to 0xFFFFFFFC");
        CHECK((cpu.flags & CPUJ_FLAG_C) != 0, "SUB 5-9 Carry (borrow)");
        CHECK((cpu.flags & CPUJ_FLAG_N) != 0, "SUB 5-9 Negative");
        cpuj_free(&cpu);
    }
    {
        cpuj_t cpu; if (!cpuj_init(&cpu)) return 1;
        (void)cpuj_alu(&cpu, OP_CMP, 10, 10);
        CHECK((cpu.flags & CPUJ_FLAG_Z) != 0, "CMP equal sets Zero");
        cpuj_free(&cpu);
    }

    /* ── stack tests ── */
    {
        cpuj_t cpu; if (!cpuj_init(&cpu)) return 1;
        cpuj_push(&cpu, 0x12345678);
        cpuj_push(&cpu, 0xABCDEF01);
        CHECK(cpu.sp == 0xFFFFFFE8, "SP decremented twice (0xFFFFFFF0 -> 0xFFFFFFE8)");
        CHECK(cpuj_pop(&cpu) == 0xABCDEF01, "LIFO pop (1)");
        CHECK(cpuj_pop(&cpu) == 0x12345678, "LIFO pop (2)");
        CHECK(cpu.sp == 0xFFFFFFF0, "SP restored after pops");
        cpuj_free(&cpu);
    }

    /* ── assembler: countdown 3..0 with JGE ── */
    {
        const char *src =
            "; countdown 3..0\n"
            "    MOVI R0, #3\n"
            "loop:\n"
            "    MOV R1, R0\n"
            "    SUBI R0, #1\n"
            "    JGE @loop\n"
            "    HALT\n";
        asm_result_t res;
        CHECK(asm_assemble(src, &res), "assembly ok");
        if (!res.error[0]) {
            CHECK(res.labels[0].addr == 4 && strcmp(res.labels[0].name, "loop") == 0,
                  "label 'loop' at address 4");
            CHECK(res.nbytes == 20, "MOVI+MOV+SUBI+JGE+HALT = 20 bytes");
            /* MOVI R0,#3 at 0, MOV at 4, SUBI at 8, JGE rel at 12, HALT at 16 */
            CHECK(res.code[0] == 0x04 && res.code[1] == 0x00 &&
                  res.code[2] == 0x00 && res.code[3] == 0x03,
                  "first instr is MOVI R0,#3");
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[1] == 0, "loop captures 0 on final pass");
            CHECK((cpu.flags & CPUJ_FLAG_N) != 0, "last SUBI left Negative set");
            cpuj_free(&cpu);
        }
    }

    /* ── run: MOVI + register ALU ── */
    {
        const char *src =
            "    MOVI R0, #5\n"
            "    MOVI R1, #3\n"
            "    ADD  R0, R1\n"
            "    SUBI R2, #2\n"
            "    XOR  R0, R2\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[0] == (8 ^ 0xFFFFFFFE), "8 ^ 0xFFFFFFFE = 0xFFFFFFF6");
            CHECK(cpu.halted, "program halts cleanly");
            cpuj_free(&cpu);
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
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[0] == 8, "stack swap + add gives R0 == 8");
            CHECK(cpu.r[1] == 5, "R1 restored to 5 after swap");
            CHECK(cpu.sp == 0xFFFFFFF0, "stack balanced after swap");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble swap program"); }
    }

    /* ── run: CALL/RET subroutine ── */
    {
        const char *src =
            "    MOVI R1, #3\n"
            "    CALL @inc2\n"
            "    HALT\n"
            "inc2:\n"
            "    ADDI R1, #2\n"
            "    RET\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[1] == 5, "CALL/RET subroutine leaves R1 == 5");
            CHECK(cpu.sp == 0xFFFFFFF0, "stack balanced after CALL/RET");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble CALL/RET program"); }
    }

    /* ── run: pointer LD/ST loop (word access) ── */
    {
        const char *src =
            "    MOVI R1, #0x40\n"
            "    MOVI R2, #3\n"
            "loop:\n"
            "    ST [R1], R2\n"
            "    ADDI R1, #4\n"
            "    SUBI R2, #1\n"
            "    JGT @loop\n"
            "    MOVI R1, #0x40\n"
            "    LD R0, [R1]\n"
            "    LD R3, [R1+4]\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpuj_mem_read32(&cpu, 0x40) == 3, "mem[0x40] word == 3");
            CHECK(cpuj_mem_read32(&cpu, 0x44) == 2, "mem[0x44] word == 2");
            CHECK(cpuj_mem_read32(&cpu, 0x48) == 1, "mem[0x48] word == 1");
            CHECK(cpu.ram[0x40] == 0 && cpu.ram[0x43] == 3, "word stored big-endian");
            CHECK(cpu.r[0] == 3, "LD [R1] reloaded 3");
            CHECK(cpu.r[3] == 2, "LD [R1+4] gave 2");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble pointer LD/ST loop"); }
    }

    /* ── run: byte LDB/STB around RAM ── */
    {
        const char *src =
            "    MOVI R0, #0x41    ; 'A'\n"
            "    MOVI R1, #0x100\n"
            "    STB [R1], R0\n"
            "    LDB R2, [R1]\n"
            "    MOVI R0, #0x42    ; 'B'\n"
            "    MOVI R1, #0x500\n"
            "    STB [R1], R0\n"
            "    LDB R3, [R1]\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.ram[0x100] == 0x41, "STB stored byte at 0x100");
            CHECK(cpu.r[2] == 0x41, "LDB loaded 0x41 back");
            CHECK(cpu.r[3] == 0x42, "LDB loaded 0x42 back");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble byte LDB/STB"); }
    }

    /* ── run: full 32-bit address via MOVU + ORI ── */
    {
        const char *src =
            "    MOVU R0, #0x123\n"
            "    ORI  R0, #0x4567\n"
            "    MOVI R1, #0x41    ; 'A'\n"
            "    STB [R0], R1\n"
            "    LDB R2, [R0]\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[0] == 0x12304567, "MOVU+ORI builds full 32-bit address");
            CHECK(cpu.ram[0x12304567] == 0x41, "STB reached a 4 GiB address");
            CHECK(cpu.r[2] == 0x41, "LDB loaded back from the far address");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble MOVU/ORI far address program"); }
    }

    /* ── run: shifts and logic (32-bit) ── */
    {
        const char *src =
            "    MOVI R0, #1\n"
            "    SHLI R0, #4       ; 16\n"
            "    SHLI R0, #1       ; 32\n"
            "    SHRI R0, #2       ; 8\n"
            "    MOVI R1, #0x0F0F\n"
            "    XOR  R0, R1       ; 0x0F07\n"
            "    MOVI R2, #0xFF\n"
            "    AND  R0, R2       ; 0x07\n"
            "    NOT  R0           ; 0xFFFFFFF8\n"
            "    MOVI R2, #1\n"
            "    SHL  R0, R2       ; shift by 1 = 0xFFFFFFF0\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[0] == 0xFFFFFFF0u, "shifts + logic give 0xFFFFFFF0");
            cpuj_free(&cpu);
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
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[1] == 0, "JEQ taken; skipped MOVI R1");
            CHECK(cpu.r[2] == 1, "reached target label");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble JEQ program"); }
    }

    /* ── run: forward jump ── */
    {
        char src[2048];
        int off = sprintf(src, "    JMP @theend\n");
        for (int i = 0; i < 40; i++) off += sprintf(src + off, "    NOP\n");
        off += sprintf(src + off, "theend:\n    MOVI R1, #7\n    HALT\n");
        (void)off;
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            CHECK(res.nbytes == 172, "layout: 4 + 40 NOPs + MOVI + HALT = 172");
            CHECK(res.code[0] == 0x60 && res.code[1] == 0x00 &&
                  res.code[2] == 0x00 && res.code[3] == 0xA0,
                  "JMP rel = 0xA0 bytes (160)");
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[1] == 7, "forward jump lands on target");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble forward-jump program"); }
    }

    /* ── run: backward loop with JNE (yes loops) ── */
    {
        const char *src =
            "    MOVI R5, #3\n"
            "loop:\n"
            "    SUBI R5, #1\n"
            "    JNE @loop\n"
            "    MOVI R2, #42\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[5] == 0, "backward JNE loop exits with R5 == 0");
            CHECK(cpu.r[2] == 42, "code after loop reached");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble backward JNE loop"); }
    }

    /* ── run: PRINT_STR traps emit expected bytes ── */
    {
        const char *src =
            "    MOVI R1, #0x100\n"
            "    MOVI R0, #65    ; 'A'\n"
            "    STB [R1], R0\n"
            "    ADDI R1, #1\n"
            "    MOVI R0, #66    ; 'B'\n"
            "    STB [R1], R0\n"
            "    ADDI R1, #1\n"
            "    MOVI R0, #0\n"
            "    STB [R1], R0\n"
            "    MOVI R0, #0x100\n"
            "    TRAP PRINT_STR\n"
            "    HALT\n";
        asm_result_t res;
        if (!asm_assemble(src, &res)) {
            printf("FAIL: PRINT_STR assemble: %s\n", res.error);
            failures++;
        } else {
            cpuj_t cpu;
            if (!cpuj_init(&cpu)) return 1;
            for (int i = 0; i < res.nbytes; i++)
                cpuj_mem_write(&cpu, (uint32_t)i, res.code[i]);
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
            cpuj_free(&cpu);
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
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[0] == 15, "hex literal 0x0F");
            CHECK(cpu.r[1] == 10, "binary literal 0b1010");
            CHECK(cpu.r[2] == 8,  "octal literal 010");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble literal formats"); }
    }

    /* ── run: 32-bit register widths ── */
    {
        const char *src =
            "    MOVI R3, #0x80000\n"
            "    MOV R4, R3\n"
            "    ADD R3, R3        ; 0x100000\n"
            "    MOVU R6, #0x7FF\n"
            "    ORI  R6, #0xFFFFF ; 0x7FFFFFFF\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj_t cpu;
            if (!run_prog(&cpu, &res)) return 1;
            CHECK(cpu.r[4] == 0x80000, "MOV preserves 0x80000");
            CHECK(cpu.r[3] == 0x100000, "0x80000+0x80000 = 0x100000");
            CHECK(cpu.r[6] == 0x7FFFFFFF, "MOVU+ORI make 0x7FFFFFFF");
            cpuj_free(&cpu);
        } else { CHECK(false, "assemble 32-bit width program"); }
    }

    /* ── assembler error handling ── */
    {
        asm_result_t res;
        CHECK(!asm_assemble("    BOGUS R0\n", &res), "unknown mnemonic rejected");
        CHECK(!asm_assemble("    MOVI R0, #1\n    JMP @missing\n", &res),
              "unknown label rejected");
        CHECK(!asm_assemble("    LD R0, [0x100]\n", &res),
              "absolute LD rejected (needs base register)");
        CHECK(!asm_assemble("    SHLI R0, #32\n", &res), "shift 32 rejected");
        CHECK(!asm_assemble("    LD R0, [R1+0x100000]\n", &res),
              "LD offset too large rejected");
        CHECK(!asm_assemble("    MOVI R0, #0x100000\n", &res),
              "MOVI immediate overflow rejected");
    }

    /* ── disassembler sanity ── */
    {
        char buf[80];
        cpuj_disassemble(CPUJ_MOVI(3, 0x12345), buf, sizeof buf);
        CHECK(strstr(buf, "MOVI") != NULL, "disassembler prints MOVI");
        cpuj_disassemble(CPUJ_JEQ(-4), buf, sizeof buf);
        CHECK(strstr(buf, "JEQ") != NULL, "disassembler prints JEQ");
        cpuj_disassemble(CPUJ_MOVU(0, 0x123), buf, sizeof buf);
        CHECK(strstr(buf, "MOVU") != NULL, "disassembler prints MOVU");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}