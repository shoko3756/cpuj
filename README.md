## cpuj

you might be asking, tf is this?

well its simple, its a whole emulated cpu

doesent have much features right now but it is improving

instructions:

| Instruction | Name | Format | Extension |
|-------------|------|--------|-----------|
| MOV | Move Register | `Rd, Rs` | ALU |
| MOVI | Move Immediate | `Rd, #imm8` | ALU |
| ADD | Add | `Rd, Rs` / `Rd, #imm8` | ALU |
| SUB | Subtract | `Rd, Rs` / `Rd, #imm8` | ALU |
| AND | Bitwise AND | `Rd, Rs` / `Rd, #imm8` | ALU |
| OR | Bitwise OR | `Rd, Rs` / `Rd, #imm8` | ALU |
| XOR | Bitwise XOR | `Rd, Rs` / `Rd, #imm8` | ALU |
| NOT | Bitwise NOT | `Rd` | ALU |
| SHL | Shift Left | `Rd, #n` | ALU |
| SHR | Shift Right | `Rd, #n` | ALU |
| CMP | Compare | `Rd, Rs` / `Rd, #imm8` | ALU |
| LD | Load | `Rd, [addr8]` / `Rd, [Rk]` | Memory |
| ST | Store | `[addr8], Rs` / `[Rk], Rs` | Memory |
| PUSH | Push Register | `Rs` | Stack |
| POP | Pop Register | `Rd` | Stack |
| JMP | Jump | `addr8` | Control |
| JEQ | Jump if Equal | `addr8` | Control |
| JNE | Jump if Not Equal | `addr8` | Control |
| JGT | Jump if Greater | `addr8` | Control |
| CALL | Call Subroutine | `addr8` | Control |
| RET | Return | — | Control |
| TRAP | Software Trap | `code` | I/O |
| HALT | Halt | — | System |
| NOP | No Operation | — | System |

# WARNING, THIS IS MADE BY AI
