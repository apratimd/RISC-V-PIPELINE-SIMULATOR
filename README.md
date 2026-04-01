# RISC-V 5-Stage Pipeline Simulator
A cycle-accurate, 5-stage pipeline simulator for a subset of the 32-bit RISC-V Instruction Set Architecture (RV32I). Written entirely in standard C (C99).

This simulator accurately models the five classic RISC pipeline stages (Instruction Fetch, Instruction Decode, Execution, Memory Access, and Write Back) handling structural, data, and control hazards dynamically without external frameworks.

## Features
* **Full Data Forwarding Unit:** Implements complex cycle-by-cycle EX-to-EX and MEM-to-EX forwarding networks, eliminating costly bubbles for back-to-back arithmetic operations.
* **Hazard Detection Unit:** Dynamically issues pipeline stalls (bubbles) cleanly handling unavoidable Load-Use Data Hazards. 
* **Control Hazard Resolution:** Pipeline flushes automatically execute when branches are taken, mitigating structural path divergence.
* **Lexer-Robust Parsing:** The built-in assembly parser safely normalizes missing commas, parens, trailing whitespaces, and code comments (`//` and `#`).
* **Memory Bounds Checking:** Secures the simulated SRAM limits, immediately throwing OS-agnostic traces when code escapes allocated constraints.
* **Interactive CLI Degubber**: Step cycle-by-cycle manually using the terminal to evaluate architectural latches.

## Supported Instructions
The parser currently supports standard 32-bit RISC-V instructions:
* **Arithmetic & Logic (`R/I` Types):** `add`, `addi`, `sub`, `and`, `andi`, `or`, `ori`, `xor`, `xori`, `sll`, `slli`, `srl`, `srli`, `sra`, `srai`, `slt`, `slti`, `sltu`, `sltiu`
* **Memory Operations (`I/S` Types):** `lw`, `lh`, `lhu`, `lb`, `lbu`, `sw`, `sh`, `sb`
* **Control Flow (`B/J` Types):** `beq`, `bne`, `blt`, `bge`, `bltu`, `bgeu`, `jal`, `jalr`
* **Upper Immediates (`U` Types):** `lui`, `auipc`
* **Control:** `halt`

## Getting Started

### 1. Compilation
The simulator is encapsulated within a single `.c` file named `pipeline_simulator.c`. Compile it securely using any standard C compiler (GCC recommended):
```bash
gcc pipeline_simulator.c -o sim -Wall -Wextra
```

### 2. File Setup
The simulator requires two input files:
- **`instructions.txt`**: The assembly instructions you want to process, one per line.
- **`data.txt`**: The pre-loaded Data Memory SRAM format. Expected strictly as `<byte_address> <integer_value>` parameters line-by-line.

Example `instructions.txt`
```assembly
# Execute an array load loop
addi x1, x0, 10
lw x3, 0(x1)
halt
```

### 3. Execution
Run the executable by passing your target instructions text file via the `-i` parameter. By default, it will look for `data.txt` for memory loads.
```bash
./sim -i instructions.txt -d data.txt
```

### 4. Interactive Debugging Mode
Need to explicitly trace latches cycle-by-cycle? Supply the `-s` (or `--step`) flag!
```bash
./sim -i instructions.txt -s
```
* **Step**: Press `Enter` to process one hardware cycle.
* **Resume**: Press `c` + `Enter` to exit interactive mode and simulate fully to the final `halt`!

## Outputs
At the successful end of the simulation execution, the program will dynamically output:
1. The Final Total Cycle count
2. The architectural state of all 32 hardware registers (with their named aliases, e.g. `ra (x1) = 5`)
3. A `dump.txt` artifact capturing the entire residual state of the Data SRAM!
