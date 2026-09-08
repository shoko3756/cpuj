CC      := cc
CFLAGS  := -std=c11 -Wall -Wextra -Wno-unused-parameter -O2 -g
SRCS    := main.c cpuj.c asm.c dbg.c
HDRS    := cpuj.h asm.h dbg.h
TARGET  := cpujvm

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

# Native test of the CPU core + assembler (no CLI)
test: tests/test_core.c $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -I. -o /tmp/cpuj_test tests/test_core.c cpuj.c asm.c
	/tmp/cpuj_test && echo "ALL TESTS PASSED"

demo: $(TARGET)
	./$(TARGET) examples/count.asm
	@echo

clean:
	rm -f $(TARGET) /tmp/cpuj_test

.PHONY: all test demo clean