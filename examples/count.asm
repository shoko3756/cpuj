; count from 42 down to 0, print each number
    MOVI R0, #42
loop:
    TRAP PRINT_REG
    SUB  R0, #1
    JNE  @loop

; print a newline for good measure
    MOVI R0, #10
    TRAP PRINT_CHAR
    HALT