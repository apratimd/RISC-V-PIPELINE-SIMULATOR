#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_LEN 64
#define REG_COUNT 32
#define IMEM_SIZE 256
#define DMEM_SIZE 256


///////////////////////////////////////////////////////// OPCODES ///////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum {
    OP_ADD, OP_SUB, OP_SLL, OP_SLT, OP_SLTU, OP_XOR, OP_SRL, OP_SRA, OP_OR, OP_AND,
    OP_ADDI, OP_SLTI, OP_SLTIU, OP_XORI, OP_ORI, OP_ANDI, OP_SLLI, OP_SRLI, OP_SRAI,
    OP_LW, OP_LH, OP_LB, OP_LHU, OP_LBU, OP_SW, OP_SH, OP_SB,
    OP_BEQ, OP_BNE, OP_BLT, OP_BGE, OP_BLTU, OP_BGEU,
    OP_LUI, OP_AUIPC, OP_JAL, OP_JALR, OP_HALT, OP_NOP
} opcode_t;


/////////////////////////////////////////////////////// CONTROL SIGNALS /////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    int RegWrite, ALUSrc, MemRead, MemWrite, MemToReg, Branch, Jump;
} control_t;

control_t control(opcode_t op) {
    control_t c = {0};

    switch (op) {
        // R-TYPE
        case OP_ADD: case OP_SUB: case OP_SLL: case OP_SLT: 
        case OP_SLTU: case OP_XOR: case OP_SRL: case OP_SRA: 
        case OP_OR: case OP_AND:
            c.RegWrite = 1;
            break;

        // I-TYPE (ALU)
        case OP_ADDI: case OP_SLTI: case OP_SLTIU: case OP_XORI: 
        case OP_ORI: case OP_ANDI: case OP_SLLI: case OP_SRLI: case OP_SRAI:
            c.RegWrite = 1;
            c.ALUSrc = 1;
            break;

        // I-TYPE (LOADS)
        case OP_LW: case OP_LH: case OP_LB: case OP_LHU: case OP_LBU:
            c.RegWrite = 1;
            c.ALUSrc = 1;
            c.MemRead = 1;
            c.MemToReg = 1;
            break;

        // S-TYPE (STORES)
        case OP_SW: case OP_SH: case OP_SB:
            c.ALUSrc = 1;
            c.MemWrite = 1;
            break;

        // B-TYPE (BRANCHES)
        case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGE: 
        case OP_BLTU: case OP_BGEU:
            c.ALUSrc = 0; // Uses RS2 directly for comparison
            c.Branch = 1;
            break;

        // U-TYPE & J-TYPE
        case OP_LUI: case OP_AUIPC:
            c.RegWrite = 1;
            c.ALUSrc = 1;
            break;
        case OP_JAL: case OP_JALR:
            c.RegWrite = 1;
            c.Jump = 1;
            break;

        default: break;
    }
    return c;
}

/////////////////////////////////////////////////////////// PIPELINE REGISTERS ////////////////////////////////////////////////////////////////////////////////////

typedef struct { int valid, pc; char instr[MAX_LEN]; } IF_ID_t;
typedef struct { int valid, pc, rs1, rs2, rd, imm, v1, v2; opcode_t op; control_t ctrl; } ID_EX_t;
typedef struct { int valid, alu, rd, store_val; opcode_t op; control_t ctrl; } EX_MEM_t;
typedef struct { int valid, alu, mem_data, rd; opcode_t op; control_t ctrl; } MEM_WB_t;


///////////////////////////////////////////////////// GLOBAL STATE //////////////////////////////////////////////////////////////////////////////////////////////



int reg_file[REG_COUNT], data_memory[DMEM_SIZE], pc = 0, cycle = 0, halt_fetched = 0, halt_done = 0;
char instruction_memory[IMEM_SIZE][MAX_LEN];
IF_ID_t IF_ID = {0};
ID_EX_t ID_EX_old = {0}, ID_EX_new = {0};
EX_MEM_t EX_MEM_old = {0}, EX_MEM_new = {0};
MEM_WB_t MEM_WB_old = {0}, MEM_WB_new = {0};







/////////////////////////////////////////////////////////////////// Pipeline stages ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////// ID STAGE ////////////////////////////////////////////////////////////////////////////////////////////

void ID_stage() {
    if (!IF_ID.valid) {
        ID_EX_new.valid = 0;
        return;
    }

    ID_EX_new = (ID_EX_t){0};
    ID_EX_new.valid = 1;
    ID_EX_new.pc = IF_ID.pc;

    char op[16];
    sscanf(IF_ID.instr, "%s", op);

    // --- R-TYPE: instr rd, rs1, rs2 ---
    if (!strcmp(op, "add"))  { ID_EX_new.op = OP_ADD;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "sub"))  { ID_EX_new.op = OP_SUB;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "sll"))  { ID_EX_new.op = OP_SLL;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "srl"))  { ID_EX_new.op = OP_SRL;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "sra"))  { ID_EX_new.op = OP_SRA;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "xor"))  { ID_EX_new.op = OP_XOR;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "or"))   { ID_EX_new.op = OP_OR;   sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }
    else if (!strcmp(op, "and"))  { ID_EX_new.op = OP_AND;  sscanf(IF_ID.instr, "%*s x%d,x%d,x%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.rs2); }

    // --- I-TYPE (ALU): instr rd, rs1, imm ---
    else if (!strcmp(op, "addi")) { ID_EX_new.op = OP_ADDI; sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.imm); }
    else if (!strcmp(op, "slli")) { ID_EX_new.op = OP_SLLI; sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.imm); }
    else if (!strcmp(op, "srli")) { ID_EX_new.op = OP_SRLI; sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.imm); }
    else if (!strcmp(op, "srai")) { ID_EX_new.op = OP_SRAI; sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.imm); }

    // --- I-TYPE (LOADS): instr rd, imm(rs1) ---
    else if (!strcmp(op, "lw"))   { ID_EX_new.op = OP_LW;   sscanf(IF_ID.instr, "%*s x%d,%d(x%d)", &ID_EX_new.rd, &ID_EX_new.imm, &ID_EX_new.rs1); }
    else if (!strcmp(op, "lb"))   { ID_EX_new.op = OP_LB;   sscanf(IF_ID.instr, "%*s x%d,%d(x%d)", &ID_EX_new.rd, &ID_EX_new.imm, &ID_EX_new.rs1); }
    else if (!strcmp(op, "lh"))   { ID_EX_new.op = OP_LH;   sscanf(IF_ID.instr, "%*s x%d,%d(x%d)", &ID_EX_new.rd, &ID_EX_new.imm, &ID_EX_new.rs1); }

    // --- S-TYPE (STORES): instr rs2, imm(rs1) ---
    else if (!strcmp(op, "sw"))   { ID_EX_new.op = OP_SW;   sscanf(IF_ID.instr, "%*s x%d,%d(x%d)", &ID_EX_new.rs2, &ID_EX_new.imm, &ID_EX_new.rs1); }
    else if (!strcmp(op, "sb"))   { ID_EX_new.op = OP_SB;   sscanf(IF_ID.instr, "%*s x%d,%d(x%d)", &ID_EX_new.rs2, &ID_EX_new.imm, &ID_EX_new.rs1); }

    // --- B-TYPE (BRANCHES): instr rs1, rs2, imm ---
    else if (!strcmp(op, "beq"))  { ID_EX_new.op = OP_BEQ;  sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rs1, &ID_EX_new.rs2, &ID_EX_new.imm); }
    else if (!strcmp(op, "bne"))  { ID_EX_new.op = OP_BNE;  sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rs1, &ID_EX_new.rs2, &ID_EX_new.imm); }
    else if (!strcmp(op, "blt"))  { ID_EX_new.op = OP_BLT;  sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rs1, &ID_EX_new.rs2, &ID_EX_new.imm); }
    else if (!strcmp(op, "bge"))  { ID_EX_new.op = OP_BGE;  sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rs1, &ID_EX_new.rs2, &ID_EX_new.imm); }

    // --- U-TYPE & J-TYPE ---
    else if (!strcmp(op, "lui"))   { ID_EX_new.op = OP_LUI;   sscanf(IF_ID.instr, "%*s x%d,%d", &ID_EX_new.rd, &ID_EX_new.imm); }
    else if (!strcmp(op, "auipc")) { ID_EX_new.op = OP_AUIPC; sscanf(IF_ID.instr, "%*s x%d,%d", &ID_EX_new.rd, &ID_EX_new.imm); }
    else if (!strcmp(op, "jal"))   { ID_EX_new.op = OP_JAL;   sscanf(IF_ID.instr, "%*s x%d,%d", &ID_EX_new.rd, &ID_EX_new.imm); }
    else if (!strcmp(op, "jalr"))  { ID_EX_new.op = OP_JALR;  sscanf(IF_ID.instr, "%*s x%d,x%d,%d", &ID_EX_new.rd, &ID_EX_new.rs1, &ID_EX_new.imm); }
    
    else if (!strcmp(op, "halt"))  { ID_EX_new.op = OP_HALT; }
    else { ID_EX_new.op = OP_NOP; }

    // Load-Use Hazard Detection
    // If previous instruction was a Load and it writes to a register this instruction needs, we stall.
    if (ID_EX_old.valid && ID_EX_old.ctrl.MemRead) {
        if (ID_EX_old.rd != 0 && (ID_EX_old.rd == ID_EX_new.rs1 || ID_EX_old.rd == ID_EX_new.rs2)) {
            ID_EX_new.valid = 0; // Turn this cycle into a bubble
            pc -= 4;             // Prevent PC from moving so we re-fetch the same instruction
            printf("ID  : STALL (Load-Use Hazard detected)\n");
            return;
        }
    }

    ID_EX_new.ctrl = control(ID_EX_new.op);
    ID_EX_new.v1 = reg_file[ID_EX_new.rs1];
    ID_EX_new.v2 = reg_file[ID_EX_new.rs2];
}
//////////////////////////////////////////////////// FORWARDING UNIT /////////////////////////////////////////////////////////////////////////////////////////////////

int forward_ex(int rs, int val) {
    if (rs == 0) return 0; // x0 is always 0

    // Forward from EX/MEM (Prioritize most recent result)
    if (EX_MEM_old.valid && EX_MEM_old.ctrl.RegWrite && EX_MEM_old.rd == rs) {
        return EX_MEM_old.alu;
    }

    // Forward from MEM/WB
    if (MEM_WB_old.valid && MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd == rs) {
        if (MEM_WB_old.ctrl.MemToReg) return MEM_WB_old.mem_data;
        return MEM_WB_old.alu;
    }

    return val;
}

////////////////////////////////////////////////////////////////////////////EX STAGE //////////////////////////////////////////////////////////////////////////////////////////
void EX_stage() {
    if (!ID_EX_old.valid) {
        EX_MEM_new.valid = 0;
        printf("EX  : BUBBLE\n");
        return;
    }

    EX_MEM_new.valid = 1;
    EX_MEM_new.op = ID_EX_old.op;
    EX_MEM_new.ctrl = ID_EX_old.ctrl;
    EX_MEM_new.rd = ID_EX_old.rd;

    // --- FORWARDING LOGIC ---
    int a = forward_ex(ID_EX_old.rs1, ID_EX_old.v1);
    int b = ID_EX_old.ctrl.ALUSrc ? ID_EX_old.imm : forward_ex(ID_EX_old.rs2, ID_EX_old.v2);
    int rs2_val = forward_ex(ID_EX_old.rs2, ID_EX_old.v2); // For stores and branches

    // --- ALU OPERATIONS ---
    switch (ID_EX_old.op) {
        case OP_ADD:  case OP_ADDI: EX_MEM_new.alu = a + b; break;
        case OP_SUB:                EX_MEM_new.alu = a - b; break;
        case OP_AND:  case OP_ANDI: EX_MEM_new.alu = a & b; break;
        case OP_OR:   case OP_ORI:  EX_MEM_new.alu = a | b; break;
        case OP_XOR:  case OP_XORI: EX_MEM_new.alu = a ^ b; break;
        case OP_SLL:  case OP_SLLI: EX_MEM_new.alu = a << (b & 0x1F); break;
        case OP_SRL:  case OP_SRLI: EX_MEM_new.alu = (unsigned int)a >> (b & 0x1F); break;
        case OP_SRA:  case OP_SRAI: EX_MEM_new.alu = a >> (b & 0x1F); break;
        case OP_SLT:  case OP_SLTI: EX_MEM_new.alu = (a < b) ? 1 : 0; break;
        case OP_SLTU: case OP_SLTIU: EX_MEM_new.alu = ((unsigned int)a < (unsigned int)b) ? 1 : 0; break;
        case OP_LUI:                EX_MEM_new.alu = ID_EX_old.imm << 12; break;
        case OP_AUIPC:              EX_MEM_new.alu = ID_EX_old.pc + (ID_EX_old.imm << 12); break;
        case OP_JAL:  case OP_JALR:  EX_MEM_new.alu = ID_EX_old.pc + 4; break; // Save return address
        default:                    EX_MEM_new.alu = a + b; break;
    }

    EX_MEM_new.store_val = rs2_val;

    // --- BRANCH AND JUMP HANDLING ---
    int take_branch = 0;
    if (ID_EX_old.ctrl.Branch) {
        switch (ID_EX_old.op) {
            case OP_BEQ:  take_branch = (a == rs2_val); break;
            case OP_BNE:  take_branch = (a != rs2_val); break;
            case OP_BLT:  take_branch = (a < rs2_val); break;
            case OP_BGE:  take_branch = (a >= rs2_val); break;
            case OP_BLTU: take_branch = ((unsigned int)a < (unsigned int)rs2_val); break;
            case OP_BGEU: take_branch = ((unsigned int)a >= (unsigned int)rs2_val); break;
            default: break;
        }
    }

    if (take_branch || ID_EX_old.op == OP_JAL || ID_EX_old.op == OP_JALR) {
        if (ID_EX_old.op == OP_JALR) 
            pc = (a + ID_EX_old.imm) & ~1; // JALR sets LSB to 0
        else 
            pc = ID_EX_old.pc + ID_EX_old.imm; // Branch or JAL

        // Flush IF/ID to simulate 1-cycle penalty for control hazard
        IF_ID.valid = 0;
        printf("EX  : CONTROL HAZARD | Redirecting PC to %d | Flushed IF/ID\n", pc);
    }

    printf("EX  : ALU=%-5d | EX/MEM : rd=%d alu=%d\n", 
           EX_MEM_new.alu, EX_MEM_new.rd, EX_MEM_new.alu);
}

////////////////////////////////////////////////////////////////// MEM STAGE //////////////////////////////////////////////////////////////////////////////////////////
void MEM_stage() {
    if (!EX_MEM_old.valid) {
        MEM_WB_new.valid = 0;
        printf("MEM : IDLE\n");
        return;
    }

    MEM_WB_new.valid = 1;
    MEM_WB_new.op = EX_MEM_old.op;
    MEM_WB_new.ctrl = EX_MEM_old.ctrl;
    MEM_WB_new.rd = EX_MEM_old.rd;
    MEM_WB_new.alu = EX_MEM_old.alu;

    int addr = EX_MEM_old.alu;
    int word_addr = addr / 4;
    int byte_offset = addr % 4;

    // --- MEMORY READ (LOADS) ---
    if (EX_MEM_old.ctrl.MemRead) {
        int raw_word = data_memory[word_addr];
        
        switch (EX_MEM_old.op) {
            case OP_LB:  // Load Byte (Signed)
                MEM_WB_new.mem_data = (signed char)((raw_word >> (byte_offset * 8)) & 0xFF);
                break;
            case OP_LBU: // Load Byte (Unsigned)
                MEM_WB_new.mem_data = (unsigned char)((raw_word >> (byte_offset * 8)) & 0xFF);
                break;
            case OP_LH:  // Load Half (Signed)
                MEM_WB_new.mem_data = (signed short)((raw_word >> (byte_offset * 8)) & 0xFFFF);
                break;
            case OP_LHU: // Load Half (Unsigned)
                MEM_WB_new.mem_data = (unsigned short)((raw_word >> (byte_offset * 8)) & 0xFFFF);
                break;
            case OP_LW:  // Load Word
            default:
                MEM_WB_new.mem_data = raw_word;
                break;
        }
        printf("MEM : LOAD mem[%d] = %d\n", addr, MEM_WB_new.mem_data);
    }

    // --- MEMORY WRITE (STORES) ---
    if (EX_MEM_old.ctrl.MemWrite) {
        int mask, val_to_store;
        int current_mem = data_memory[word_addr];

        switch (EX_MEM_old.op) {
            case OP_SB: // Store Byte
                mask = 0xFF << (byte_offset * 8);
                val_to_store = (EX_MEM_old.store_val & 0xFF) << (byte_offset * 8);
                data_memory[word_addr] = (current_mem & ~mask) | val_to_store;
                break;
            case OP_SH: // Store Half
                mask = 0xFFFF << (byte_offset * 8);
                val_to_store = (EX_MEM_old.store_val & 0xFFFF) << (byte_offset * 8);
                data_memory[word_addr] = (current_mem & ~mask) | val_to_store;
                break;
            case OP_SW: // Store Word
            default:
                data_memory[word_addr] = EX_MEM_old.store_val;
                break;
        }
        printf("MEM : STORE mem[%d] = %d\n", addr, EX_MEM_old.store_val);
    }
}

//////////////////////////////////////////////////////////////// WB STAGE ///////////////////////////////////////////////////////////////////////////////////////////

void WB_stage() {
    if (!MEM_WB_old.valid) return;
    if (MEM_WB_old.op == OP_HALT) { halt_done = 1; return; }
    if (MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd != 0)
        reg_file[MEM_WB_old.rd] = MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.mem_data : MEM_WB_old.alu;
}


///////////////////////////////////////////////////////////// DUMP DATA MEMORY ///////////////////////////////////////////////////////////////////////////////////
void dump_data_memory(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        printf("Error: Could not create %s\n", filename);
        return;
    }

    fprintf(fp, "Address | Decimal    | Hexadecimal\n");
    fprintf(fp, "-----------------------------------\n");
    for (int i = 0; i < DMEM_SIZE; i++) {
        // Only dump memory that isn't zero to keep the file readable
        if (data_memory[i] != 0) {
            fprintf(fp, "%07d | %-10d | 0x%08X\n", i * 4, data_memory[i], data_memory[i]);
        }
    }
    fclose(fp);
    printf("Final data memory state dumped to %s\n", filename);
}

////////////////////////////////////////////////////////////// MAIN FUNCTION /////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[]) {
    printf("DEBUG: argc=%d, argv[0]=%s, argv[1]=%s\n", argc, argv[0], (argc > 1 ? argv[1] : "NONE"));
    // Determine which file to open
    char *inst_file = (argc > 1) ? argv[1] : "instructions.txt";

    // 1. Initialize Architectural State
    memset(reg_file, 0, sizeof(reg_file));
    pc = 0;
    cycle = 0;
    halt_fetched = 0;
    halt_done = 0;
    // Clear pipeline latches
    IF_ID = (IF_ID_t){0};
    ID_EX_old = ID_EX_new = (ID_EX_t){0};
    EX_MEM_old = EX_MEM_new = (EX_MEM_t){0};
    MEM_WB_old = MEM_WB_new = (MEM_WB_t){0};

    // 2. Load Data Memory (Always loads from data.txt)
    load_data_memory("data.txt");

    // 3. Load Instruction Memory (from the specified file)
    FILE *ifp = fopen(inst_file, "r");
    if (!ifp) {
        printf("Error: Could not open %s\n", inst_file);
        return 1;
    }
    int n = 0;
    while (n < IMEM_SIZE && fgets(instruction_memory[n], MAX_LEN, ifp)) {
        instruction_memory[n][strcspn(instruction_memory[n], "\r\n")] = 0;
        n++;
    }
    fclose(ifp);
    printf("--- Loaded %d instructions from %s ---\n", n, inst_file);

    // 4. Simulation Loop
    while (!halt_done || IF_ID.valid || ID_EX_old.valid || EX_MEM_old.valid || MEM_WB_old.valid) {
        cycle++;
        // (Optional) printf("\n--- CYCLE %d ---\n", cycle);

        WB_stage();
        MEM_stage();
        EX_stage();
        ID_stage();

        // IF Stage
        if (!halt_fetched && (pc / 4) < n) {
            if (ID_EX_new.valid || !IF_ID.valid) { 
                IF_ID.valid = 1;
                IF_ID.pc = pc;
                strcpy(IF_ID.instr, instruction_memory[pc / 4]);
                if (strstr(IF_ID.instr, "halt")) halt_fetched = 1;
                pc += 4;
            }
        } else {
            IF_ID.valid = 0;
        }

        ID_EX_old  = ID_EX_new;
        EX_MEM_old = EX_MEM_new;
        MEM_WB_old = MEM_WB_new;

        if (cycle > 10000) break; 
    }

    // 5. Final Report
    printf("\nTEST RESULT for %s:\n", inst_file);
    printf("Total Cycles: %d\n", cycle);
    for (int i = 1; i < REG_COUNT; i++) {
        if (reg_file[i] != 0) printf("  x%d = %d\n", i, reg_file[i]);
    }
    
    char dump_name[MAX_LEN];
    sprintf(dump_name, "dump_%s", inst_file);
    dump_data_memory(dump_name);

    return 0;
}
