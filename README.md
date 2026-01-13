# RISC-V 5-Stage In-Order Pipeline Simulator

## Overview

This project implements a **cycle-accurate, in-order, 5-stage RISC-V RV32I pipeline simulator** written in C.
The simulator models the classical **IF–ID–EX–MEM–WB** pipeline and explicitly represents all pipeline registers,
control signals, hazards, and forwarding paths.
---

## Pipeline Structure

The processor follows the standard 5-stage RISC-V pipeline:

1. **Instruction Fetch (IF)**  
   Fetches instructions from instruction memory using the program counter.

2. **Instruction Decode (ID)**  
   Decodes instructions, reads register operands, generates control signals, and performs load-use hazard detection.

3. **Execute (EX)**  
   Performs ALU operations, evaluates branch conditions, computes effective addresses, and applies forwarding.

4. **Memory Access (MEM)**  
   Executes load and store operations with alignment checking and sign/zero extension.

5. **Write Back (WB)**  
   Writes results back to the register file.

Each stage communicates through explicit pipeline registers:
- IF/ID
- ID/EX
- EX/MEM
- MEM/WB

---

## Supported Instruction Set (RV32I Subset)

### Arithmetic and Logical (R-type)
- add, sub
- and, or, xor
- sll, srl, sra
- slt, sltu

### Immediate Arithmetic (I-type)
- addi, andi, ori, xori
- slli, srli, srai
- slti, sltiu

### Load Instructions
- lb, lbu
- lh, lhu
- lw

### Store Instructions
- sb
- sh
- sw

### Branch Instructions
- beq, bne
- blt, bge
- bltu, bgeu

### Jump and Upper Immediate
- jal, jalr
- lui, auipc

### System Instructions
- halt (terminates simulation)
- nop

---

## Hazard Handling

### Data Hazards

#### Load-Use Hazard Detection
- Implemented in the ID stage
- Detects dependencies on a load in the immediately preceding cycle
- Inserts a pipeline bubble and freezes the PC and IF/ID register

#### Register Forwarding
- Implemented in the EX stage
- Forwarding paths:
  - EX/MEM → EX
  - MEM/WB → EX
- Eliminates unnecessary stalls for most ALU dependencies

---

### Control Hazards

- Branch and jump targets are resolved in the EX stage
- A one-cycle penalty is modeled for taken branches and jumps
- On a control transfer:
  - The PC is redirected
  - The IF/ID pipeline register is flushed

---

## Memory System

- Word-addressed data memory
- Supports byte, halfword, and word accesses
- Signed and unsigned load variants are implemented
- Strict alignment checks for word and halfword accesses

---

## Simulation Model

- Cycle-accurate simulation
- Each cycle executes the stages in the following order:
  1. Write Back
  2. Memory
  3. Execute
  4. Decode
  5. Fetch
- Pipeline registers are updated once per cycle
- Simulation terminates only after:
  - A halt instruction completes
  - All pipeline stages are drained

---

## Input Files

### Instruction File

Instructions are provided in a text file (default: instructions.txt), with one instruction per line,
using RISC-V-like assembly syntax.

Example:
```
addi x1,x0,10
addi x2,x0,20
add  x3,x1,x2
sw   x3,0(x0)
lw   x4,0(x0)
halt
```

A different instruction file may be passed as a command-line argument.

---

### Data Memory Initialization

Data memory is initialized from data.txt.

Format:
```
<address> <value>
```

Addresses are byte addresses and are internally mapped to word indices.

---

## Output

### Console Output
- Cycle-by-cycle pipeline activity
- Stall notifications
- Control hazard redirection messages
- Final register file state

### Data Memory Dump
- At the end of execution, non-zero memory locations are written to:
  dump_<instruction_file>

---

## Compilation and Execution

### Compile
```
gcc pipeline.c -o pipeline
```

### Run
```
./pipeline instructions.txt
```

---

## Design Philosophy

This simulator prioritizes:
- Correctness over performance
- Explicit modeling of microarchitectural behavior
- Readability and extensibility

It is intentionally limited to an in-order, single-issue pipeline and does not model:
- Out-of-order execution
- Caches or memory hierarchies
- Branch prediction
- Exceptions or CSR handling

---

## Possible Extensions

- Static or dynamic branch prediction
- Cache and memory hierarchy modeling
- Exception and interrupt handling
- Superscalar issue
- Tomasulo’s algorithm or scoreboarding
- RV64I support

---

## Author

Apratim Dev Goswami  


## License

This project is intended for educational and research use.
Reuse and modification are permitted with appropriate attribution.


This project is intended for educational and research use.
Reuse and modification are permitted with appropriate attribution.
