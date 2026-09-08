; build a string in RAM and print it via PRINT_STR
;
; writes  "hi!"  + NUL  starting at address 0x100 using R1 as a pointer
; (0x100 sits past the program and clear of the stack at 0xFFF0)

    MOVI R1, #0x100

    MOVI R0, #0x68        ; 'h'
    ST [R1], R0
    ADD  R1, #1

    MOVI R0, #0x69        ; 'i'
    ST [R1], R0
    ADD  R1, #1

    MOVI R0, #0x21        ; '!'
    ST [R1], R0
    ADD  R1, #1

    MOVI R0, #0x00        ; NUL terminator
    ST [R1], R0

    MOVI R0, #0x100       ; point R0 at the string
    TRAP PRINT_STR
    MOVI R0, #10          ; newline
    TRAP PRINT_CHAR
    HALT