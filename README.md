## cpuj

you might be asking, tf is this?

well its simple, its a whole emulated cpu

doesent have much features right now but it is improving

it's a 32-bit CPU now: 8 registers (R0-R7), 4 GiB of RAM, and every
instruction is one fixed 32-bit word (`[op:6][rd:3][rs:3][imm:20]`).
MOVU + ORI can build any 32-bit value, so the whole address space is
reachable.

instructions:

| Instruction | Name | Format | Extension |
|-------------|------|--------|-----------|
| MOV | Move Register | `Rd, Rs` | ALU |
| MOVI | Move Immediate | `Rd, #imm20` | ALU |
| MOVU | Move Upper Immediate | `Rd, #n` (`n << 20`) | ALU |
| ADD | Add | `Rd, Rs` | ALU |
| ADDI | Add Immediate | `Rd, #imm20` | ALU |
| SUB | Subtract | `Rd, Rs` | ALU |
| SUBI | Subtract Immediate | `Rd, #imm20` | ALU |
| AND | Bitwise AND | `Rd, Rs` | ALU |
| ANDI | Bitwise AND Immediate | `Rd, #imm20` | ALU |
| OR | Bitwise OR | `Rd, Rs` | ALU |
| ORI | Bitwise OR Immediate | `Rd, #imm20` | ALU |
| XOR | Bitwise XOR | `Rd, Rs` | ALU |
| XORI | Bitwise XOR Immediate | `Rd, #imm20` | ALU |
| NOT | Bitwise NOT | `Rd` | ALU |
| SHL | Shift Left | `Rd, Rs` | ALU |
| SHLI | Shift Left Immediate | `Rd, #n` (0..31) | ALU |
| SHR | Shift Right | `Rd, Rs` | ALU |
| SHRI | Shift Right Immediate | `Rd, #n` (0..31) | ALU |
| CMP | Compare | `Rd, Rs` | ALU |
| CMPI | Compare Immediate | `Rd, #imm20` | ALU |
| LD | Load Word | `Rd, [Rs+off]` | Memory |
| ST | Store Word | `[Rs+off], Rd` | Memory |
| LDB | Load Byte | `Rd, [Rs+off]` | Memory |
| STB | Store Byte | `[Rs+off], Rd` | Memory |
| JMP | Unconditional Jump | `rel20` | Control |
| JEQ | Jump if Equal | `rel20` | Control |
| JNE | Jump if Not Equal | `rel20` | Control |
| JGT | Jump if Greater | `rel20` | Control |
| JGE | Jump if Greater or Equal | `rel20` | Control |
| JLT | Jump if Less Than | `rel20` | Control |
| JLE | Jump if Less or Equal | `rel20` | Control |
| JMPR | Jump Register | `Rs` | Control |
| CALL | Call Subroutine | `rel20` | Control |
| CALLR | Call Register | `Rs` | Control |
| PUSH | Push Register | `Rd` | Stack |
| POP | Pop Register | `Rd` | Stack |
| RET | Return | — | Control |
| TRAP | Software Trap | `code` | I/O |
| HALT | Halt | — | System |
| NOP | No Operation | — | System |

branches and CALL use a signed 20-bit offset in bytes from the next
instruction (`±0x80000`); there is no absolute form, so load a target
into a register with MOVI/MOVU and use JMPR/CALLR for far jumps or
indirect calls. LD/ST/LDB/STB need a base register — load addresses
with MOVI/MOVU too.


run `cpuj sh` to get into shell
# WARNING, THIS IS MADE BY AI