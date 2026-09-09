; sh.asm — a tiny shell that runs on the cpuj VM (32-bit ISA)
;
; builtins:  help   echo <text>   count   regs   exit   quit
;            sm     ls   uname   yes   say <text>   mem
;
; Storage layout (strings are written into RAM at startup because the
; assembler has no data directive):
;   0x1200  prompt  "$ "
;   0x1210  "help"      0x1220 "echo"     0x1230 "count"
;   0x1240  "regs"      0x1250 "exit"     0x1260 "quit"
;   0x1270  banner "cpuj sh"
;   0x1280  hint "commands: help echo count regs exit sm ls uname yes say mem"
;   0x12E0  "sm"        0x12F0 "ls"       0x1300 "uname"
;   0x1310  "yes"       0x1320 "say"      0x1330 "mem"
;   0x1340  "cpuj"      0x1350 "y\n"
;   0x1380  "sm: smoke test"
;   0x1390  "cpuj: unknown command '"     0x13B0 "'\n"
;   0x1400  input line buffer
;
; registers: R0/R1 scratch, R2/R3 string compare, R4/R5 streq scratch,
;            R6 = input line buffer base (0x1400)

; ── start: jump to the shell entry point ─────────────────────────────
    JMP @main

; ── putc: write R0 to [R1], then R1++ ────────────────────────────────
putc:
    STB [R1], R0
    ADDI R1, #1
    RET

; ── streq: compare string at R2 with string at R3 ────────────────────
;    on return the Z flag is set iff they are equal
streq:
    PUSH R4
    PUSH R5
strcmp_loop:
    LDB R4, [R2]
    LDB R5, [R3]
    CMP R4, R5
    JNE @strcmp_ne
    CMP R4, #0
    JEQ @strcmp_eq
    ADDI R2, #1
    ADDI R3, #1
    JMP @strcmp_loop
strcmp_ne:
    POP R5
    POP R4
    RET
strcmp_eq:
    POP R5
    POP R4
    RET

; ── readline: read chars into 0x1400 until newline ───────────────────
;    on return R3 = 1 on EOF, else 0; line is NUL-terminated
readline:
    MOVI R2, #0x1400
read_loop:
    TRAP READ_CHAR          ; R0 = char, 0 on EOF
    CMP R0, #0
    JEQ @read_eof
    CMP R0, #10             ; '\n'
    JEQ @read_done
    CMP R0, #13             ; '\r' — ignore
    JEQ @read_loop
    STB [R2], R0
    ADDI R2, #1
    JMP @read_loop
read_done:
    MOVI R0, #0
    STB [R2], R0
    MOVI R3, #0
    RET
read_eof:
    MOVI R3, #1
    RET

; ── start ────────────────────────────────────────────────────────────

main:
    ; build the strings used by the shell
    MOVI R1, #0x1200
    MOVI R0, #0x24          ; '$'
    CALL @putc
    MOVI R0, #0x20          ; ' '
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1210        ; "help"
    MOVI R0, #0x68
    CALL @putc
    MOVI R0, #0x65
    CALL @putc
    MOVI R0, #0x6C
    CALL @putc
    MOVI R0, #0x70
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1220        ; "echo"
    MOVI R0, #0x65
    CALL @putc
    MOVI R0, #0x63
    CALL @putc
    MOVI R0, #0x68
    CALL @putc
    MOVI R0, #0x6F
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1230        ; "count"
    MOVI R0, #0x63
    CALL @putc
    MOVI R0, #0x6F
    CALL @putc
    MOVI R0, #0x75
    CALL @putc
    MOVI R0, #0x6E
    CALL @putc
    MOVI R0, #0x74
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1240        ; "regs"
    MOVI R0, #0x72
    CALL @putc
    MOVI R0, #0x65
    CALL @putc
    MOVI R0, #0x67
    CALL @putc
    MOVI R0, #0x73
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1250        ; "exit"
    MOVI R0, #0x65
    CALL @putc
    MOVI R0, #0x78
    CALL @putc
    MOVI R0, #0x69
    CALL @putc
    MOVI R0, #0x74
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1260        ; "quit"
    MOVI R0, #0x71
    CALL @putc
    MOVI R0, #0x75
    CALL @putc
    MOVI R0, #0x69
    CALL @putc
    MOVI R0, #0x74
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1270        ; "cpuj sh"
    MOVI R0, #0x63
    CALL @putc
    MOVI R0, #0x70
    CALL @putc
    MOVI R0, #0x75
    CALL @putc
    MOVI R0, #0x6A
    CALL @putc
    MOVI R0, #0x20
    CALL @putc
    MOVI R0, #0x73
    CALL @putc
    MOVI R0, #0x68
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1280        ; "commands: help echo count regs exit sm ls uname yes say mem"
    MOVI R0, #0x63  ; c
    CALL @putc
    MOVI R0, #0x6F  ; o
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x61  ; a
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x64  ; d
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x3A  ; :
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x68  ; h
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x6C  ; l
    CALL @putc
    MOVI R0, #0x70  ; p
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x63  ; c
    CALL @putc
    MOVI R0, #0x68  ; h
    CALL @putc
    MOVI R0, #0x6F  ; o
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x63  ; c
    CALL @putc
    MOVI R0, #0x6F  ; o
    CALL @putc
    MOVI R0, #0x75  ; u
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x74  ; t
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x72  ; r
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x67  ; g
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x78  ; x
    CALL @putc
    MOVI R0, #0x69  ; i
    CALL @putc
    MOVI R0, #0x74  ; t
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x6C  ; l
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x75  ; u
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x61  ; a
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x79  ; y
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x61  ; a
    CALL @putc
    MOVI R0, #0x79  ; y
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1390        ; "cpuj: unknown command '"
    MOVI R0, #0x63  ; c
    CALL @putc
    MOVI R0, #0x70  ; p
    CALL @putc
    MOVI R0, #0x75  ; u
    CALL @putc
    MOVI R0, #0x6A  ; j
    CALL @putc
    MOVI R0, #0x3A  ; :
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x75  ; u
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x6B  ; k
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x6F  ; o
    CALL @putc
    MOVI R0, #0x77  ; w
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x63  ; c
    CALL @putc
    MOVI R0, #0x6F  ; o
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x61  ; a
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x64  ; d
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x27  ; '
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x13B0        ; "'\n"
    MOVI R0, #0x27  ; '
    CALL @putc
    MOVI R0, #0x0A  ; \n
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x12E0        ; "sm"
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x12F0        ; "ls"
    MOVI R0, #0x6C  ; l
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1300        ; "uname"
    MOVI R0, #0x75  ; u
    CALL @putc
    MOVI R0, #0x6E  ; n
    CALL @putc
    MOVI R0, #0x61  ; a
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1310        ; "yes"
    MOVI R0, #0x79  ; y
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1320        ; "say"
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x61  ; a
    CALL @putc
    MOVI R0, #0x79  ; y
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1330        ; "mem"
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1340        ; "cpuj"
    MOVI R0, #0x63  ; c
    CALL @putc
    MOVI R0, #0x70  ; p
    CALL @putc
    MOVI R0, #0x75  ; u
    CALL @putc
    MOVI R0, #0x6A  ; j
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1350        ; "y\n"
    MOVI R0, #0x79  ; y
    CALL @putc
    MOVI R0, #0x0A  ; \n
    CALL @putc
    MOVI R0, #0
    CALL @putc

    MOVI R1, #0x1380        ; "sm: smoke test"
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x3A  ; :
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x6D  ; m
    CALL @putc
    MOVI R0, #0x6F  ; o
    CALL @putc
    MOVI R0, #0x6B  ; k
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x20  ; ' '
    CALL @putc
    MOVI R0, #0x74  ; t
    CALL @putc
    MOVI R0, #0x65  ; e
    CALL @putc
    MOVI R0, #0x73  ; s
    CALL @putc
    MOVI R0, #0x74  ; t
    CALL @putc
    MOVI R0, #0
    CALL @putc

    ; banner
    MOVI R0, #0x1270
    TRAP PRINT_STR
    MOVI R0, #10
    TRAP PRINT_CHAR

; ── main loop ────────────────────────────────────────────────────────

    MOVI R6, #0x1400        ; line buffer base for peek loads

shell_loop:
    MOVI R0, #0x1200        ; prompt "$ "
    TRAP PRINT_STR

    CALL @readline
    CMP R3, #1              ; EOF?
    JEQ @eof

    LDB R0, [R6]            ; skip empty lines
    CMP R0, #0
    JEQ @shell_loop

    MOVI R2, #0x1400
    MOVI R3, #0x1210        ; "help"
    CALL @streq
    JEQ @do_help

    MOVI R2, #0x1400
    MOVI R3, #0x1220        ; "echo"
    CALL @streq
    JEQ @do_echo
    ; "echo <text>" — check the prefix outright
    LDB R0, [R6+0]
    CMP R0, #0x65          ; 'e'
    JNE @echo_no
    LDB R0, [R6+1]
    CMP R0, #0x63          ; 'c'
    JNE @echo_no
    LDB R0, [R6+2]
    CMP R0, #0x68          ; 'h'
    JNE @echo_no
    LDB R0, [R6+3]
    CMP R0, #0x6F          ; 'o'
    JNE @echo_no
    LDB R0, [R6+4]
    CMP R0, #0x20          ; ' '
    JEQ @do_echo
echo_no:

    MOVI R2, #0x1400
    MOVI R3, #0x1230        ; "count"
    CALL @streq
    JEQ @do_count

    MOVI R2, #0x1400
    MOVI R3, #0x1240        ; "regs"
    CALL @streq
    JEQ @do_regs

    MOVI R2, #0x1400
    MOVI R3, #0x1250        ; "exit"
    CALL @streq
    JEQ @do_exit

    MOVI R2, #0x1400
    MOVI R3, #0x1260        ; "quit"
    CALL @streq
    JEQ @do_exit

    ; "sm" — smoke test only
    LDB R0, [R6+0]
    CMP R0, #0x73          ; 's'
    JNE @sm_no
    LDB R0, [R6+1]
    CMP R0, #0x6D          ; 'm'
    JNE @sm_no
    LDB R0, [R6+2]
    CMP R0, #0             ; end of "sm"
    JEQ @do_sm
sm_no:

    MOVI R2, #0x1400
    MOVI R3, #0x12F0        ; "ls"
    CALL @streq
    JEQ @do_ls

    MOVI R2, #0x1400
    MOVI R3, #0x1300        ; "uname"
    CALL @streq
    JEQ @do_uname

    MOVI R2, #0x1400
    MOVI R3, #0x1310        ; "yes"
    CALL @streq
    JEQ @do_yes

    MOVI R2, #0x1400
    MOVI R3, #0x1330        ; "mem"
    CALL @streq
    JEQ @do_mem

    MOVI R2, #0x1400
    MOVI R3, #0x1320        ; "say"
    CALL @streq
    JEQ @do_say
    ; "say <text>" — check the prefix outright
    LDB R0, [R6+0]
    CMP R0, #0x73          ; 's'
    JNE @say_no
    LDB R0, [R6+1]
    CMP R0, #0x61          ; 'a'
    JNE @say_no
    LDB R0, [R6+2]
    CMP R0, #0x79          ; 'y'
    JNE @say_no
    LDB R0, [R6+3]
    CMP R0, #0x20          ; ' '
    JEQ @do_say
say_no:

    ; unknown command
    MOVI R0, #0x1390
    TRAP PRINT_STR
    MOVI R0, #0x1400
    TRAP PRINT_STR
    MOVI R0, #0x13B0        ; "'\n"
    TRAP PRINT_STR
    JMP @shell_loop

; ── builtins ─────────────────────────────────────────────────────────

do_help:
    MOVI R0, #0x1280
    TRAP PRINT_STR
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

do_echo:
    MOVI R2, #0x1400
echo_scan:
    LDB R0, [R2]
    CMP R0, #0
    JEQ @echo_nl
    CMP R0, #32             ; space
    JEQ @echo_start
    ADDI R2, #1
    JMP @echo_scan
echo_start:
    ADDI R2, #1
    LDB R0, [R2]
    CMP R0, #32
    JEQ @echo_start
    CMP R0, #0
    JEQ @echo_nl
    MOV R0, R2
    TRAP PRINT_STR
echo_nl:
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

do_count:
    MOVI R0, #42
count_loop:
    TRAP PRINT_REG
    SUB R0, #1
    JGE @count_loop
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

do_regs:
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R1
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R2
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R3
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R4
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R5
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R6
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOV R0, R7
    TRAP PRINT_REG
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

; ── top-level commands ────────────────────────────────────────────────
;    sm            run a smoke test
;    ls            list the builtin commands
;    uname         print the machine name
;    yes           print y a few times
;    say <text>    print text
;    mem           hex-dump a chunk of memory

do_sm:
    MOVI R0, #0x1380         ; "sm: smoke test"
    TRAP PRINT_STR
    MOVI R0, #10
    TRAP PRINT_CHAR
    MOVI R0, #42
do_sm_loop:
    TRAP PRINT_REG
    SUB R0, #1
    JGE @do_sm_loop
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

do_ls:
    MOVI R0, #0x1280
    TRAP PRINT_STR
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

do_uname:
    MOVI R0, #0x1340        ; "cpuj"
    TRAP PRINT_STR
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

do_yes:
    MOVI R5, #10
do_yes_loop:
    MOVI R0, #0x1350        ; "y\n"
    TRAP PRINT_STR
    SUB R5, #1
    JNE @do_yes_loop
    JMP @shell_loop

do_say:
    JMP @do_echo

do_mem:
    MOVI R1, #0x1200
    MOVI R5, #16             ; bytes to dump
do_mem_loop:
    LDB R0, [R1]
    CALL @hex_byte
    MOVI R0, #0x20           ; ' '
    TRAP PRINT_CHAR
    ADDI R1, #1
    SUB R5, #1
    JNE @do_mem_loop
    MOVI R0, #10
    TRAP PRINT_CHAR
    JMP @shell_loop

; ── hex_byte: print R0 as two hex digits ──────────────────────────────

hex_byte:
    PUSH R0
    SHRI R0, #4
    AND R0, #0xF
    CALL @hex_char
    POP R0
    AND R0, #0xF
    CALL @hex_char
    RET

hex_char:
    CMP R0, #10
    JGE @hex_char_ge
    ADD R0, #48              ; '0'
    JMP @hex_char_out
hex_char_ge:
    ADD R0, #55              ; 'A' - 10
hex_char_out:
    TRAP PRINT_CHAR
    RET

do_exit:
    HALT

eof:
    MOVI R0, #10
    TRAP PRINT_CHAR
    HALT