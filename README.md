## cpuj

you might be asking, tf is this?

well its simple, its a whole emulated cpu

doesent have much features right now but it is improving

instructions:

| Instruction | Name | Format | Extension |
|-------------|------|--------|-----------|
| MOV | Move Register | `Rd, Rs` | ALU |
| MOVI | Move Immediate | `Rd, #imm16` | ALU |
| ADD | Add | `Rd, Rs` / `Rd, #imm16` | ALU |
| SUB | Subtract | `Rd, Rs` / `Rd, #imm16` | ALU |
| AND | Bitwise AND | `Rd, Rs` / `Rd, #imm16` | ALU |
| OR | Bitwise OR | `Rd, Rs` / `Rd, #imm16` | ALU |
| XOR | Bitwise XOR | `Rd, Rs` / `Rd, #imm16` | ALU |
| NOT | Bitwise NOT | `Rd` | ALU |
| SHL | Shift Left | `Rd, Rs` / `Rd, #n` | ALU |
| SHR | Shift Right | `Rd, Rs` / `Rd, #n` | ALU |
| CMP | Compare | `Rd, Rs` / `Rd, #imm16` | ALU |
| LD | Load | `Rd, [Rs+off]` / `Rd, [addr16]` | Memory |
| ST | Store | `[Rs+off], Rd` / `[addr16], Rd` | Memory |
| PUSH | Push Register | `Rd` | Stack |
| POP | Pop Register | `Rd` | Stack |
| JMP | Unconditional Jump | `off6` / `addr16` | Control |
| JEQ | Jump if Equal | `off6` / `addr16` | Control |
| JNE | Jump if Not Equal | `off6` / `addr16` | Control |
| JGT | Jump if Greater | `off6` / `addr16` | Control |
| JGE | Jump if Greater or Equal | `off6` / `addr16` | Control |
| JLT | Jump if Less Than | `off6` / `addr16` | Control |
| JLE | Jump if Less or Equal | `off6` / `addr16` | Control |
| CALL | Call Subroutine | `addr16` / `Rs` | Control |
| RET | Return | — | Control |
| TRAP | Software Trap | `code` | I/O |
| HALT | Halt | — | System |
| NOP | No Operation | — | System |

# WARNING, THIS IS MADE BY AI
