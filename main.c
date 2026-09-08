#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "cpuj1.h"
#include "asm.h"
#include "dbg.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "cpuj1 — 8-bit virtual CPU (architecture v1)\n\n"
        "usage:\n"
        "  %s <file.asm>              assemble and run\n"
        "  %s <file.asm> -d           assemble and run under debugger\n"
        "  %s <file.bin> -x           load raw binary and run\n"
        "  %s <file.bin> -x -d        load raw binary under debugger\n"
        "  %s -h                      this help\n",
        prog, prog, prog, prog, prog);
}

/* Read an entire text file into a malloc'd buffer. */
static char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len) *out_len = (long)got;
    return buf;
}

int main(int argc, char **argv) {
    bool debug = false, raw = false;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) debug = true;
        else if (strcmp(argv[i], "-x") == 0) raw = true;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (path == NULL) path = argv[i];
        else { usage(argv[0]); return 1; }
    }

    if (!path) { usage(argv[0]); return 1; }

    cpuj1_t cpu;
    cpuj1_init(&cpu);

    asm_result_t prog;
    bool have_prog = false;

    if (raw) {
        long len = 0;
        char *data = read_file(path, &len);
        if (!data) { fprintf(stderr, "cannot open %s\n", path); return 1; }
        for (long i = 0; i < len; i++)
            cpuj1_mem_write(&cpu, (uint16_t)i, (uint8_t)data[i]);
        free(data);
        printf("loaded %ld bytes of raw binary\n", len);
    } else {
        long len = 0;
        char *src = read_file(path, &len);
        if (!src) { fprintf(stderr, "cannot open %s\n", path); return 1; }
        if (!asm_assemble(src, &prog)) {
            fprintf(stderr, "assembly failed: %s\n", prog.error);
            free(src);
            return 1;
        }
        free(src);
        have_prog = true;

        /* load words (big-endian) at successive addresses */
        for (int i = 0; i < prog.count && prog.total_bytes + 1 < CPUJ1_RAM_SIZE; i++) {
            int addr = i * 2;
            cpuj1_mem_write(&cpu, (uint16_t)addr,        (uint8_t)(prog.words[i] >> 8));
            cpuj1_mem_write(&cpu, (uint16_t)(addr + 1),  (uint8_t)(prog.words[i]));
        }
        printf("assembled %d instructions (%d bytes) from %s\n",
               prog.count, prog.total_bytes, path);
    }

    dbg_t dbg = { 0 };

    if (debug) {
        dbg_run(&cpu, have_prog ? &prog : NULL, &dbg);
    } else {
        long max_ticks = 1 << 20;
        while (!cpu.halted && max_ticks-- > 0) {
            if (have_prog && cpu.pc >= (uint16_t)prog.total_bytes) {
                cpu.halted = true;
                break;
            }
            cpuj1_tick(&cpu);
        }
        if (cpu.halted) {
            printf("\ncpuj1 halted after %ld cycles.\n", (long)((1 << 20) - max_ticks));
        } else {
            printf("runaway program — hit max cycle limit. use -d to debug.\n");
        }
    }

    return 0;
}