#include <stdio.h>
#include <string.h>
#include "cpuj1.h"
#include "asm.h"

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
         else { printf("ok:   %s\n", msg); } } while (0)

int main(int argc, char **argv) {
    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

    /* --- instruction encoding unit tests --- */
    /* encoding: [op:4][rd:2][rs:2][imm8] */
    CHECK(CPUJ1_MOV(1, 2) == 0x0600, "MOV R1,R2 encoding");
    CHECK(CPUJ1_MOVI(3, 0x42) == 0x1C42, "MOVI R3,#0x42 encoding");
    CHECK(CPUJ1_ADD_IMM(0, 5) == 0x2305, "ADD R0,#5 encoding");
    CHECK(CPUJ1_ADD_REG(1, 0) == 0x2400, "ADD R1,R0 encoding");
    CHECK(CPUJ1_SUB_IMM(2, 1) == 0x3B01, "SUB R2,#1");
    CHECK(CPUJ1_JMP(0x40) == 0xD040, "JMP 0x40");
    CHECK(CPUJ1_JEQ(0x40) == 0xD140, "JEQ 0x40");
    CHECK(CPUJ1_CALL(0x30) == 0xF030, "CALL 0x30");
    CHECK(CPUJ1_PUSH(2) == 0xE200, "PUSH R2");
    CHECK(CPUJ1_POP(1) == 0xE420, "POP R1");
    CHECK(CPUJ1_TRAP(0) == 0xE040, "TRAP PRINT_REG");
    CHECK(CPUJ1_HALT() == 0xE050, "HALT");

    /* --- ALU flag + value tests --- */
    {
        cpuj1_t cpu;
        cpuj1_init(&cpu);
        cpu.r[0] = 0xFF;
        uint8_t r = cpuj1_alu(&cpu, OP_ADD, cpu.r[0], 1);
        CHECK(r == 0x00, "ADD 0xFF+1 == 0");
        CHECK((cpu.flags & CPUJ1_FLAG_C) != 0, "ADD 0xFF+1 sets Carry");
        CHECK((cpu.flags & CPUJ1_FLAG_Z) != 0, "ADD 0xFF+1 sets Zero");
    }
    {
        cpuj1_t cpu;
        cpuj1_init(&cpu);
        cpu.r[0] = 0x80;
        uint8_t r = cpuj1_alu(&cpu, OP_ADD, cpu.r[0], 0x80);
        CHECK(r == 0x00, "ADD 0x80+0x80 == 0");
        CHECK((cpu.flags & CPUJ1_FLAG_C) != 0, "ADD 0x80+0x80 Carry");
        CHECK((cpu.flags & CPUJ1_FLAG_N) == 0, "ADD 0x80+0x80 not Negative");
        (void)r;
    }
    {
        cpuj1_t cpu;
        cpuj1_init(&cpu);
        cpu.r[0] = 5;
        uint8_t r = cpuj1_alu(&cpu, OP_SUB, cpu.r[0], 9);
        CHECK(r == (uint8_t)-4, "SUB 5-9 wraps to 252");
        CHECK((cpu.flags & CPUJ1_FLAG_C) != 0, "SUB 5-9 sets Carry (borrow)");
        CHECK((cpu.flags & CPUJ1_FLAG_N) != 0, "SUB 5-9 sets Negative");
    }

    /* --- stack --- */
    {
        cpuj1_t cpu;
        cpuj1_init(&cpu);
        cpuj1_push(&cpu, 0xAA);
        cpuj1_push(&cpu, 0xBB);
        CHECK(cpu.sp == 0xFD, "SP decremented twice");
        CHECK(cpuj1_pop(&cpu) == 0xBB, "LIFO pop (1)");
        CHECK(cpuj1_pop(&cpu) == 0xAA, "LIFO pop (2)");
    }

    /* --- assembler: full program with labels --- */
    {
        const char *src =
            "; countdown 3..0\n"
            "    MOVI R0, #3\n"
            "loop:\n"
            "    TRAP PRINT_REG\n"
            "    SUB R0, #1\n"
            "    JNE @loop\n"
            "    HALT\n";
        asm_result_t res;
        CHECK(asm_assemble(src, &res), "assembly ok");
        if (verbose) {
            for (int i = 0; i < res.count; i++)
                printf("  %04X\n", res.words[i]);
        }
        int lbl = -1;
        for (int i = 0; i < res.nlabels; i++)
            if (strcmp(res.labels[i].name, "loop") == 0) lbl = res.labels[i].addr;
        CHECK(lbl == 2, "label 'loop' at address 2");
        CHECK(res.count == 5, "5 instructions");
        CHECK(res.words[0] == 0x1003, "first instr MOVI R0,#3");
    }

    /* --- run a real program: swap R0/R1 via stack, then add --- */
    {
        const char *src =
            "    MOVI R0, #5\n"
            "    MOVI R1, #3\n"
            "    PUSH R0\n"
            "    PUSH R1\n"
            "    POP R0\n"
            "    POP R1\n"   /* swap R0,R1 via stack */
            "    ADD R0, R1  ; R0 should now be 8\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj1_t cpu;
            cpuj1_init(&cpu);
            for (int i = 0; i < res.count; i++) {
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2), (uint8_t)(res.words[i] >> 8));
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2 + 1), (uint8_t)res.words[i]);
            }
            for (int i = 0; !cpu.halted && i < 1000; i++)
                cpuj1_tick(&cpu);
            CHECK(cpu.r[0] == 8, "stack swap + add gives R0 == 8");
            CHECK(cpu.r[1] == 5, "R1 restored to 5 after swap");
        } else {
            CHECK(false, "assemble swap program");
        }
    }

    /* --- CALL/RET: a subroutine that adds 2 to R1 --- */
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
            cpuj1_t cpu; cpuj1_init(&cpu);
            for (int i = 0; i < res.count; i++) {
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2), (uint8_t)(res.words[i] >> 8));
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2 + 1), (uint8_t)res.words[i]);
            }
            for (int i = 0; !cpu.halted && i < 1000; i++)
                cpuj1_tick(&cpu);
            CHECK(cpu.r[1] == 5, "CALL/RET subroutine leaves R1 == 5");
            CHECK(cpu.sp == 0xFF, "stack balanced after CALL/RET");
        } else {
            printf("FAIL: CALL/RET assemble: %s\n", res.error);
            failures++;
        }
    }

    /* --- pointer LD/ST + CMP/JGT loop --- */
    {
        const char *src =
            "    MOVI R1, #0x40   ; pointer to array\n"
            "    MOVI R2, #3      ; counter\n"
            "loop:\n"
            "    MOV R0, R2\n"
            "    ST [R1], R0      ; store counter byte\n"
            "    ADD R1, #1\n"
            "    SUB R2, #1\n"
            "    JGT @loop\n"
            "    ; array now holds 3,2,1 at 0x40..0x42\n"
            "    MOVI R1, #0x40   ; re-read first element\n"
            "    LD R0, [R1]\n"
            "    ADD R1, #1\n"
            "    LD R2, [R1]\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj1_t cpu; cpuj1_init(&cpu);
            for (int i = 0; i < res.count; i++) {
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2), (uint8_t)(res.words[i] >> 8));
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2 + 1), (uint8_t)res.words[i]);
            }
            for (int i = 0; !cpu.halted && i < 1000; i++)
                cpuj1_tick(&cpu);
            CHECK(cpu.ram[0x40] == 3, "array[0] == 3");
            CHECK(cpu.ram[0x41] == 2, "array[1] == 2");
            CHECK(cpu.ram[0x42] == 1, "array[2] == 1");
            CHECK(cpu.r[0] == 3, "LD [R1] reloaded 3");
            CHECK(cpu.r[2] == 2, "LD [R1+1] gave 2");
        } else {
            printf("FAIL: pointer LS assemble: %s\n", res.error);
            failures++;
        }
    }

    /* --- shifts and logic --- */
    {
        const char *src =
            "    MOVI R0, #1\n"
            "    SHL R0, #4       ; 16\n"
            "    SHL R0, #1       ; 32\n"
            "    SHR R0, #2       ; 8\n"
            "    XOR R0, #8       ; 0\n"
            "    MOVI R1, #0x0F\n"
            "    NOT R1           ; 0xF0\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj1_t cpu; cpuj1_init(&cpu);
            for (int i = 0; i < res.count; i++) {
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2), (uint8_t)(res.words[i] >> 8));
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2 + 1), (uint8_t)res.words[i]);
            }
            for (int i = 0; !cpu.halted && i < 1000; i++)
                cpuj1_tick(&cpu);
            CHECK(cpu.r[0] == 0, "shifts + xor cancel out");
            CHECK(cpu.r[1] == 0xF0, "NOT 0x0F == 0xF0");
        } else {
            printf("FAIL: shifts assemble: %s\n", res.error);
            failures++;
        }
    }

    /* --- TRAP PRINT_STR emits expected bytes --- */
    {
        const char *src =
            "    MOVI R1, #0x40\n"
            "    MOVI R0, #65    ; 'A'\n"
            "    ST [R1], R0\n"
            "    MOVI R0, #66    ; 'B'\n"
            "    ADD R1, #1\n"
            "    ST [R1], R0\n"
            "    ADD R1, #1\n"
            "    MOVI R0, #0\n"
            "    ST [R1], R0\n"
            "    MOVI R0, #0x40\n"
            "    TRAP PRINT_STR\n"
            "    HALT\n";
        asm_result_t res;
        if (!asm_assemble(src, &res)) {
            printf("FAIL: PRINT_STR assemble: %s\n", res.error);
            failures++;
        } else {
            cpuj1_t cpu; cpuj1_init(&cpu);
            for (int i = 0; i < res.count; i++) {
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2), (uint8_t)(res.words[i] >> 8));
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2 + 1), (uint8_t)res.words[i]);
            }
            FILE *cap = tmpfile();
            FILE *old = stdout;
            stdout = cap;
            for (int i = 0; !cpu.halted && i < 1000; i++)
                cpuj1_tick(&cpu);
            stdout = old;
            fflush(cap);
            rewind(cap);
            char out[64];
            if (!fgets(out, sizeof out, cap)) out[0] = '\0';
            fclose(cap);
            CHECK(strcmp(out, "AB") == 0, "PRINT_STR printed 'AB'");
        }
    }

    /* --- number literal formats: hex, binary, octal --- */
    {
        const char *src =
            "    MOVI R0, #0x0F   ; hex\n"
            "    MOVI R1, #0b1010 ; binary\n"
            "    MOVI R2, #010    ; octal = 8\n"
            "    HALT\n";
        asm_result_t res;
        if (asm_assemble(src, &res)) {
            cpuj1_t cpu; cpuj1_init(&cpu);
            for (int i = 0; i < res.count; i++) {
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2), (uint8_t)(res.words[i] >> 8));
                cpuj1_mem_write(&cpu, (uint16_t)(i * 2 + 1), (uint8_t)res.words[i]);
            }
            for (int i = 0; !cpu.halted && i < 1000; i++)
                cpuj1_tick(&cpu);
            CHECK(cpu.r[0] == 15, "hex literal 0x0F");
            CHECK(cpu.r[1] == 10, "binary literal 0b1010");
            CHECK(cpu.r[2] == 8,  "octal literal 010");
        } else {
            printf("FAIL: literals assemble: %s\n", res.error);
            failures++;
        }
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall tests passed\n", failures);
    return failures ? 1 : 0;
}