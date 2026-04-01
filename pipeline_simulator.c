#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_LEN 64
#define REG_COUNT 32
#define IMEM_SIZE 256
#define DMEM_SIZE 256

int32_t stall = 0;
int32_t pc_redirect = 0;
uint32_t pc_next = 0;

const char *reg_names[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
    "t3", "t4", "t5", "t6"
};

int32_t reg_index(const char *name) {
    if (name[0] == 'x' || name[0] == 'X') {
        int idx = atoi(name + 1);
        if (idx >= 0 && idx < 32) return idx;
    }
    for (int i = 0; i < 32; i++) {
        if (strcmp(name, reg_names[i]) == 0) return i;
    }
    printf("ERROR: Unknown register '%s'\n", name);
    exit(1);
}

typedef enum {
    OP_ADD, OP_SUB, OP_SLL, OP_SLT, OP_SLTU, OP_XOR, OP_SRL, OP_SRA, OP_OR, OP_AND,
    OP_ADDI, OP_SLTI, OP_SLTIU, OP_XORI, OP_ORI, OP_ANDI, OP_SLLI, OP_SRLI, OP_SRAI,
    OP_LW, OP_LH, OP_LB, OP_LHU, OP_LBU, OP_SW, OP_SH, OP_SB,
    OP_BEQ, OP_BNE, OP_BLT, OP_BGE, OP_BLTU, OP_BGEU,
    OP_LUI, OP_AUIPC, OP_JAL, OP_JALR, OP_HALT, OP_NOP
} opcode_t;

typedef struct {
    int32_t RegWrite, ALUSrc, MemRead, MemWrite, MemToReg, Branch, Jump;
} control_t;

control_t control(opcode_t op) {
    control_t c = {0};
    switch (op) {
        case OP_ADD: case OP_SUB: case OP_SLL: case OP_SLT: case OP_SLTU:
        case OP_XOR: case OP_SRL: case OP_SRA: case OP_OR: case OP_AND:
            c.RegWrite = 1; break;
        case OP_ADDI: case OP_SLTI: case OP_SLTIU: case OP_XORI: case OP_ORI:
        case OP_ANDI: case OP_SLLI: case OP_SRLI: case OP_SRAI:
            c.RegWrite = 1; c.ALUSrc = 1; break;
        case OP_LW: case OP_LH: case OP_LB: case OP_LHU: case OP_LBU:
            c.RegWrite = 1; c.ALUSrc = 1; c.MemRead = 1; c.MemToReg = 1; break;
        case OP_SW: case OP_SH: case OP_SB:
            c.ALUSrc = 1; c.MemWrite = 1; break;
        case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGE: case OP_BLTU: case OP_BGEU:
            c.ALUSrc = 0; c.Branch = 1; break;
        case OP_LUI: case OP_AUIPC:
            c.RegWrite = 1; c.ALUSrc = 1; break;
        case OP_JAL: case OP_JALR:
            c.RegWrite = 1; c.Jump = 1; break;
        default: break;
    }
    return c;
}

typedef struct {
    int32_t valid;
    uint32_t pc;
    char instr[MAX_LEN];
} IF_ID_t;

typedef struct {
    int32_t valid;
    uint32_t pc;
    int32_t rs1, rs2, rd, imm, v1, v2;
    opcode_t op;
    control_t ctrl;
} ID_EX_t;

typedef struct {
    int32_t valid, alu, rd, store_val;
    opcode_t op;
    control_t ctrl;
} EX_MEM_t;

typedef struct {
    int32_t valid, alu, mem_data, rd;
    opcode_t op;
    control_t ctrl;
} MEM_WB_t;

int32_t reg_file[REG_COUNT];
int32_t data_memory[DMEM_SIZE];
uint32_t pc = 0, cycle = 0;
int32_t halt_fetched = 0, halt_done = 0;
char instruction_memory[IMEM_SIZE][MAX_LEN];
IF_ID_t IF_ID = {0};
ID_EX_t ID_EX_old = {0}, ID_EX_new = {0};
EX_MEM_t EX_MEM_old = {0}, EX_MEM_new = {0};
MEM_WB_t MEM_WB_old = {0}, MEM_WB_new = {0};

void IF_stage(int instr_count) {
    if (pc_redirect) {
        pc = pc_next;
        pc_redirect = 0;
    }
    if (stall) {
        printf("IF  : STALL (PC frozen)\n");
        return;
    }
    if ((pc / 4) >= (uint32_t)instr_count) {
        IF_ID.valid = 0;
        return;
    }
    IF_ID.valid = 1;
    IF_ID.pc = pc;
    strcpy(IF_ID.instr, instruction_memory[pc / 4]);
    
    // Trim string safely
    char *trimmed = IF_ID.instr;
    while(isspace((unsigned char)*trimmed)) trimmed++;
    if (strcmp(trimmed, "halt") == 0) halt_fetched = 1;

    pc += 4;
}

void parse_instruction(char *instr, ID_EX_t *latch) {
    char clean[MAX_LEN];
    strcpy(clean, instr);
    // Erase commas and parens
    for (int i = 0; clean[i]; i++) {
        if (clean[i] == ',' || clean[i] == '(' || clean[i] == ')') clean[i] = ' ';
        // Convert to lowercase to be safe
        clean[i] = tolower((unsigned char)clean[i]);
    }

    char op[16] = {0}, a1[16] = {0}, a2[16] = {0}, a3[16] = {0};
    sscanf(clean, "%15s %15s %15s %15s", op, a1, a2, a3);

    latch->op = OP_NOP;
    
    if (!strcmp(op, "add")) { latch->op = OP_ADD; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "sub")) { latch->op = OP_SUB; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "sll")) { latch->op = OP_SLL; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "srl")) { latch->op = OP_SRL; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "sra")) { latch->op = OP_SRA; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "xor")) { latch->op = OP_XOR; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "or")) { latch->op = OP_OR; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    else if (!strcmp(op, "and")) { latch->op = OP_AND; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->rs2 = reg_index(a3); }
    // I-TYPE
    else if (!strcmp(op, "addi")) { latch->op = OP_ADDI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "slli")) { latch->op = OP_SLLI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "srli")) { latch->op = OP_SRLI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "srai")) { latch->op = OP_SRAI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "slti")) { latch->op = OP_SLTI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "sltiu")){ latch->op = OP_SLTIU; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "xori")) { latch->op = OP_XORI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "ori")) { latch->op = OP_ORI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "andi")) { latch->op = OP_ANDI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    // LOADS (rd, imm, rs1)
    else if (!strcmp(op, "lw")) { latch->op = OP_LW; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lh")) { latch->op = OP_LH; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lb")) { latch->op = OP_LB; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lhu")) { latch->op = OP_LHU; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lbu")) { latch->op = OP_LBU; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    // STORES (rs2, imm, rs1)
    else if (!strcmp(op, "sw")) { latch->op = OP_SW; latch->rs2 = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "sh")) { latch->op = OP_SH; latch->rs2 = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "sb")) { latch->op = OP_SB; latch->rs2 = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    // BRANCHES (rs1, rs2, imm)
    else if (!strcmp(op, "beq")) { latch->op = OP_BEQ; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bne")) { latch->op = OP_BNE; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "blt")) { latch->op = OP_BLT; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bge")) { latch->op = OP_BGE; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bltu")) { latch->op = OP_BLTU; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bgeu")) { latch->op = OP_BGEU; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    // U/J
    else if (!strcmp(op, "lui")) { latch->op = OP_LUI; latch->rd = reg_index(a1); latch->imm = atoi(a2); }
    else if (!strcmp(op, "auipc")) { latch->op = OP_AUIPC; latch->rd = reg_index(a1); latch->imm = atoi(a2); }
    else if (!strcmp(op, "jal")) { latch->op = OP_JAL; latch->rd = reg_index(a1); latch->imm = atoi(a2); }
    else if (!strcmp(op, "jalr")) { latch->op = OP_JALR; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    // HALT
    else if (!strcmp(op, "halt")) { latch->op = OP_HALT; latch->valid = 1; latch->ctrl = (control_t){0}; return; }
}

void ID_stage() {
    ID_EX_new = (ID_EX_t){0};
    if (stall) { ID_EX_new.valid = 0; return; }
    if (!IF_ID.valid) return;
    
    ID_EX_new.valid = 1;
    ID_EX_new.pc = IF_ID.pc;
    parse_instruction(IF_ID.instr, &ID_EX_new);

    if (IF_ID.valid && ID_EX_old.valid && ID_EX_old.ctrl.MemRead && ID_EX_old.rd != 0 &&
        (ID_EX_old.rd == ID_EX_new.rs1 || ID_EX_old.rd == ID_EX_new.rs2))
    {
        stall = 1;
        ID_EX_new = (ID_EX_t){0};
        return;
    }

    ID_EX_new.ctrl = control(ID_EX_new.op);
    ID_EX_new.v1 = reg_file[ID_EX_new.rs1];
    ID_EX_new.v2 = reg_file[ID_EX_new.rs2];
}

int32_t forward_ex(int32_t rs, int32_t val) {
    if (rs == 0) return 0;
    if (EX_MEM_old.valid && EX_MEM_old.ctrl.RegWrite && !EX_MEM_old.ctrl.MemRead && EX_MEM_old.rd == rs)
        return EX_MEM_old.alu;
    if (MEM_WB_new.valid && MEM_WB_new.ctrl.RegWrite && MEM_WB_new.ctrl.MemToReg && MEM_WB_new.rd == rs)
        return MEM_WB_new.mem_data;
    if (MEM_WB_old.valid && MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd == rs)
        return MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.mem_data : MEM_WB_old.alu;
    return val;
}

void EX_stage() {
    EX_MEM_new = (EX_MEM_t){0};
    if (!ID_EX_old.valid) { printf("EX  : BUBBLE\n"); return; }

    EX_MEM_new.valid = 1;
    EX_MEM_new.op = ID_EX_old.op;
    EX_MEM_new.ctrl = ID_EX_old.ctrl;
    EX_MEM_new.rd = ID_EX_old.rd;

    int32_t a = reg_file[ID_EX_old.rs1];
    int32_t b = ID_EX_old.ctrl.ALUSrc ? ID_EX_old.imm : reg_file[ID_EX_old.rs2];

    a = forward_ex(ID_EX_old.rs1, a);
    if (!ID_EX_old.ctrl.ALUSrc) b = forward_ex(ID_EX_old.rs2, b);

    int32_t store_val = reg_file[ID_EX_old.rs2];
    if (EX_MEM_old.valid && EX_MEM_old.ctrl.RegWrite && EX_MEM_old.rd == ID_EX_old.rs2)
        store_val = EX_MEM_old.alu;
    if (MEM_WB_old.valid && MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd == ID_EX_old.rs2)
        store_val = MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.mem_data : MEM_WB_old.alu;

    EX_MEM_new.store_val = store_val;

    switch (ID_EX_old.op) {
        case OP_ADD: case OP_ADDI: EX_MEM_new.alu = a + b; break;
        case OP_SUB: EX_MEM_new.alu = a - b; break;
        case OP_AND: case OP_ANDI: EX_MEM_new.alu = a & b; break;
        case OP_OR:  case OP_ORI: EX_MEM_new.alu = a | b; break;
        case OP_XOR: case OP_XORI: EX_MEM_new.alu = a ^ b; break;
        case OP_SLL: case OP_SLLI: EX_MEM_new.alu = a << (b & 0x1F); break;
        case OP_SRL: case OP_SRLI: EX_MEM_new.alu = (uint32_t)a >> (b & 0x1F); break;
        case OP_SRA: case OP_SRAI: EX_MEM_new.alu = a >> (b & 0x1F); break;
        case OP_SLT: case OP_SLTI: EX_MEM_new.alu = (a < b) ? 1 : 0; break;
        case OP_SLTU: case OP_SLTIU: EX_MEM_new.alu = ((uint32_t)a < (uint32_t)b) ? 1 : 0; break;
        case OP_LUI: EX_MEM_new.alu = ID_EX_old.imm << 12; break;
        case OP_AUIPC: EX_MEM_new.alu = ID_EX_old.pc + (ID_EX_old.imm << 12); break;
        case OP_JAL: case OP_JALR: EX_MEM_new.alu = ID_EX_old.pc + 4; break;
        case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGE: case OP_BLTU: case OP_BGEU: EX_MEM_new.alu = 0; break;
        default: EX_MEM_new.alu = a + b; break;
    }

    int32_t take_branch = 0;
    if (ID_EX_old.ctrl.Branch) {
        switch (ID_EX_old.op) {
            case OP_BEQ: take_branch = (a == b); break;
            case OP_BNE: take_branch = (a != b); break;
            case OP_BLT: take_branch = (a < b); break;
            case OP_BGE: take_branch = (a >= b); break;
            case OP_BLTU: take_branch = ((uint32_t)a < (uint32_t)b); break;
            case OP_BGEU: take_branch = ((uint32_t)a >= (uint32_t)b); break;
            default: break;
        }
    }

    if (take_branch || ID_EX_old.op == OP_JAL || ID_EX_old.op == OP_JALR) {
        pc_next = (ID_EX_old.op == OP_JALR) ? (uint32_t)((a + ID_EX_old.imm) & ~1) : (ID_EX_old.pc + ID_EX_old.imm);
        pc_redirect = 1;
        IF_ID.valid = 0;
        ID_EX_new.valid = 0;
    }

    if (ID_EX_old.op == OP_HALT) {
        EX_MEM_new.valid = 1; EX_MEM_new.op = OP_HALT; EX_MEM_new.ctrl = (control_t){0};
        return;
    }
    printf("EX  : ALU=%-5d | EX/MEM : rd=%d alu=%d\n", EX_MEM_new.alu, EX_MEM_new.rd, EX_MEM_new.alu);
}

void MEM_stage() {
    MEM_WB_new = (MEM_WB_t){0};
    if (!EX_MEM_old.valid) return;
    if (EX_MEM_old.op == OP_HALT) { MEM_WB_new.valid = 1; MEM_WB_new.op = OP_HALT; return; }

    MEM_WB_new.valid = 1;
    MEM_WB_new.op = EX_MEM_old.op;
    MEM_WB_new.ctrl = EX_MEM_old.ctrl;
    MEM_WB_new.rd = EX_MEM_old.rd;
    MEM_WB_new.alu = EX_MEM_old.alu;

    uint32_t addr = EX_MEM_old.alu;
    uint32_t word_addr = addr / 4;
    uint32_t byte_offset = addr % 4;
    
    // Bounds check
    if (EX_MEM_old.ctrl.MemRead || EX_MEM_old.ctrl.MemWrite) {
        if (word_addr >= DMEM_SIZE) {
            printf("ERROR: Data Memory access out of bounds at byte address %u\n", addr);
            exit(1);
        }
    }

    if ((EX_MEM_old.op == OP_SH || EX_MEM_old.op == OP_LH || EX_MEM_old.op == OP_LHU) && (addr % 2 != 0)) {
        printf("ERROR: MISALIGNED HALF ACCESS at %u\n", addr); exit(1);
    }
    if ((EX_MEM_old.op == OP_SW || EX_MEM_old.op == OP_LW) && (addr % 4 != 0)) {
        printf("ERROR: MISALIGNED WORD ACCESS at %u\n", addr); exit(1);
    }

    if (EX_MEM_old.ctrl.MemRead) {
        int32_t raw_word = data_memory[word_addr];
        switch (EX_MEM_old.op) {
            case OP_LB: MEM_WB_new.mem_data = (int8_t)((raw_word >> (byte_offset * 8)) & 0xFF); break;
            case OP_LBU: MEM_WB_new.mem_data = (uint8_t)((raw_word >> (byte_offset * 8)) & 0xFF); break;
            case OP_LH: MEM_WB_new.mem_data = (int16_t)((raw_word >> (byte_offset * 8)) & 0xFFFF); break;
            case OP_LHU: MEM_WB_new.mem_data = (uint16_t)((raw_word >> (byte_offset * 8)) & 0xFFFF); break;
            case OP_LW: default: MEM_WB_new.mem_data = raw_word; break;
        }
        printf("MEM : LOAD mem[%u] = %d\n", addr, MEM_WB_new.mem_data);
    }

    switch (EX_MEM_old.op) {
        case OP_SB: {
            uint8_t *p = (uint8_t *)&data_memory[word_addr];
            p[byte_offset] = EX_MEM_old.store_val & 0xFF;
            break;
        }
        case OP_SH: {
            uint8_t *p = (uint8_t *)&data_memory[word_addr];
            p[byte_offset] = EX_MEM_old.store_val & 0xFF;
            p[byte_offset + 1] = (EX_MEM_old.store_val >> 8) & 0xFF;
            break;
        }
        case OP_SW:
            printf("MEM : STORE HIT addr=%u word=%u value=%d\n", addr, word_addr, EX_MEM_old.store_val);
            data_memory[word_addr] = EX_MEM_old.store_val;
            break;
        default: break;
    }
}

void WB_stage() {
    if (!MEM_WB_old.valid) return;
    if (MEM_WB_old.op == OP_HALT) { halt_done = 1; return; }
    if (MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd != 0)
        reg_file[MEM_WB_old.rd] = MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.mem_data : MEM_WB_old.alu;
}

void load_data_memory(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { printf("WARNING: Unable to load data memory from '%s'. Continuing empty.\n", filename); return; }
    int addr = 0, val = 0;
    while (fscanf(fp, "%d %d", &addr, &val) != EOF) {
        if (addr / 4 < DMEM_SIZE) data_memory[addr / 4] = val;
    }
    fclose(fp);
}

void dump_data_memory(const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) return;
    for (int i = 0; i < DMEM_SIZE; i++) {
        if (data_memory[i] != 0) fprintf(fp, "%d: %d\n", i * 4, data_memory[i]);
    }
    fclose(fp);
}

int main(int argc, char *argv[]) {
    char *inst_file = "instructions.txt";
    char *data_file = "data.txt";
    bool interactive = false;

    // CLI Arguments parser
    for(int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--step")) {
            interactive = true;
        } else if (!strcmp(argv[i], "-i") && i + 1 < argc) {
            inst_file = argv[++i];
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            data_file = argv[++i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: %s [-i instructions.txt] [-d data.txt] [-s/--step]\n", argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            inst_file = argv[i];
        }
    }

    memset(reg_file, 0, sizeof(reg_file));
    memset(data_memory, 0, sizeof(data_memory));
    pc = 0; cycle = 0; halt_fetched = 0; halt_done = 0;
    
    load_data_memory(data_file);

    FILE *ifp = fopen(inst_file, "r");
    if (!ifp) { printf("ERROR: Could not open instructions file '%s'\n", inst_file); return 1; }
    uint32_t n = 0;
    char line[MAX_LEN];
    while (n < IMEM_SIZE && fgets(line, MAX_LEN, ifp)) {
        char *start = line;
        while(isspace((unsigned char)*start)) start++;
        if (*start == '\0' || *start == '#' || (start[0] == '/' && start[1] == '/')) continue; // Skip comments/empty
        
        char *end = start + strlen(start) - 1;
        while(end > start && isspace((unsigned char)*end)) end--;
        end[1] = '\0';

        strcpy(instruction_memory[n], start);
        n++;
    }
    fclose(ifp);
    printf("--- Loaded %d instructions from %s ---\n", n, inst_file);

    while (!halt_done && (IF_ID.valid || ID_EX_old.valid || EX_MEM_old.valid || MEM_WB_old.valid || (pc / 4 < n))) {
        cycle++;
        stall = 0;
        printf("\n--- CYCLE %u ---\n", cycle);
        WB_stage();
        MEM_stage();
        EX_stage();
        ID_stage();
        IF_stage(n);

        ID_EX_old = ID_EX_new;
        EX_MEM_old = EX_MEM_new;
        MEM_WB_old = MEM_WB_new;

        if (interactive && !halt_done && (IF_ID.valid || ID_EX_old.valid || EX_MEM_old.valid || MEM_WB_old.valid || (pc / 4 < n))) {
            printf("\nCycle %u complete. Press ENTER to step, or 'c' to run to end: ", cycle);
            int c = getchar();
            if (c == 'c' || c == 'C') {
                interactive = false;
                while((c = getchar()) != '\n' && c != EOF);
            } else if (c != '\n') {
                while((c = getchar()) != '\n' && c != EOF);
            }
        }
    }

    printf("\nTEST RESULT for %s:\n", inst_file);
    printf("Total Cycles: %u\n", cycle);
    for (int i = 1; i < REG_COUNT; i++) {
        if (reg_file[i] != 0) printf("  %-4s (x%d) = %d\n", reg_names[i], i, reg_file[i]);
    }

    dump_data_memory("dump.txt");
    return 0;
}
