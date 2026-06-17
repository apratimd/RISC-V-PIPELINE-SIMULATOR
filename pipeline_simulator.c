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

#define ARCH_REGS 32
#define PHYS_REGS 64
#define IQ_SIZE 16
#define FREE_LIST_SIZE 64

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
    
    int src1_phys;
    int src2_phys;
    int dest_phys;
    int old_dest_phys;
    int rs_index;
    int rob_index;
} ID_EX_t;

typedef struct {
    int32_t valid, alu, rd, store_val;
    opcode_t op;
    control_t ctrl;
    
    int dest_phys;
    int old_dest_phys;
    int rob_index;
} EX_MEM_t;

typedef struct {
    int32_t valid, alu, mem_data, rd;
    opcode_t op;
    control_t ctrl;
    
    int dest_phys;
    int old_dest_phys;
    int rob_index;
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

typedef struct {
    uint32_t pc;
    char instr[MAX_LEN];
} iq_entry_t;

iq_entry_t instruction_queue[IQ_SIZE];
int iq_head = 0;
int iq_tail = 0;
int iq_count = 0;

void iq_clear() {
    iq_head = 0;
    iq_tail = 0;
    iq_count = 0;
}

bool iq_is_empty() {
    return iq_count == 0;
}

bool iq_is_full() {
    return iq_count == IQ_SIZE;
}

void iq_enqueue(uint32_t pc, const char *instr) {
    if (iq_is_full()) {
        printf("ERROR: Instruction Queue full\n");
        exit(1);
    }
    instruction_queue[iq_tail].pc = pc;
    strncpy(instruction_queue[iq_tail].instr, instr, MAX_LEN - 1);
    instruction_queue[iq_tail].instr[MAX_LEN - 1] = '\0';
    iq_tail = (iq_tail + 1) % IQ_SIZE;
    iq_count++;
}

iq_entry_t iq_dequeue() {
    if (iq_is_empty()) {
        printf("ERROR: Instruction Queue empty\n");
        exit(1);
    }
    iq_entry_t entry = instruction_queue[iq_head];
    iq_head = (iq_head + 1) % IQ_SIZE;
    iq_count--;
    return entry;
}

typedef struct {
    int value;
    int ready;
} phys_reg_t;

phys_reg_t physical_registers[PHYS_REGS];

void init_physical_registers() {
    for (int i = 0; i < PHYS_REGS; i++) {
        physical_registers[i].value = 0;
        physical_registers[i].ready = 1;
    }
}

typedef struct {
    int phys_reg;
} RAT_entry;

RAT_entry RAT[ARCH_REGS];

void init_rat() {
    for (int i = 0; i < ARCH_REGS; i++) {
        RAT[i].phys_reg = i;
    }
}

int get_phys_reg(int arch_reg) {
    if (arch_reg < 0 || arch_reg >= ARCH_REGS) return -1;
    return RAT[arch_reg].phys_reg;
}

void set_phys_reg(int arch_reg, int phys_reg) {
    if (arch_reg >= 0 && arch_reg < ARCH_REGS) {
        RAT[arch_reg].phys_reg = phys_reg;
    }
}

int free_list[FREE_LIST_SIZE];
int fl_head = 0;
int fl_tail = 0;
int fl_count = 0;

void init_freelist() {
    fl_head = 0;
    fl_tail = 0;
    fl_count = 0;
    for (int i = ARCH_REGS; i < PHYS_REGS; i++) {
        free_list[fl_tail] = i;
        fl_tail = (fl_tail + 1) % FREE_LIST_SIZE;
        fl_count++;
    }
}

int allocate_phys_reg() {
    if (fl_count == 0) {
        return -1;
    }
    int reg = free_list[fl_head];
    fl_head = (fl_head + 1) % FREE_LIST_SIZE;
    fl_count--;
    return reg;
}

void free_phys_reg(int phys_reg) {
    if (fl_count >= FREE_LIST_SIZE) {
        printf("ERROR: Free list overflow\n");
        exit(1);
    }
    free_list[fl_tail] = phys_reg;
    fl_tail = (fl_tail + 1) % FREE_LIST_SIZE;
    fl_count++;
}

typedef struct {
    int busy;
    opcode_t op;
    int dest_phys;
    int src1_phys;
    int src2_phys;
    int src1_ready;
    int src2_ready;
    int src1_value;
    int src2_value;
} RS_entry;

RS_entry int_rs[8];

void init_rs() {
    for (int i = 0; i < 8; i++) {
        int_rs[i].busy = 0;
        int_rs[i].op = OP_NOP;
        int_rs[i].dest_phys = -1;
        int_rs[i].src1_phys = -1;
        int_rs[i].src2_phys = -1;
        int_rs[i].src1_ready = 0;
        int_rs[i].src2_ready = 0;
        int_rs[i].src1_value = 0;
        int_rs[i].src2_value = 0;
    }
}

int allocate_rs() {
    for (int i = 0; i < 8; i++) {
        if (!int_rs[i].busy) {
            int_rs[i].busy = 1;
            return i;
        }
    }
    return -1;
}

void free_rs(int index) {
    if (index >= 0 && index < 8) {
        int_rs[index].busy = 0;
    }
}

typedef struct {
    int busy;
    opcode_t op;
    int arch_dest;
    int phys_dest;
    int ready;
    int value;
} ROB_entry;

ROB_entry ROB[32];
int rob_head = 0;
int rob_tail = 0;
int rob_count = 0;

void init_rob() {
    rob_head = 0;
    rob_tail = 0;
    rob_count = 0;
    for (int i = 0; i < 32; i++) {
        ROB[i].busy = 0;
        ROB[i].op = OP_NOP;
        ROB[i].arch_dest = -1;
        ROB[i].phys_dest = -1;
        ROB[i].ready = 0;
        ROB[i].value = 0;
    }
}

int allocate_rob_entry() {
    if (rob_count == 32) {
        return -1;
    }
    int idx = rob_tail;
    rob_tail = (rob_tail + 1) % 32;
    rob_count++;
    ROB[idx].busy = 1;
    return idx;
}

void free_rob_entry(int index) {
    if (rob_count > 0 && index == rob_head) {
        ROB[rob_head].busy = 0;
        ROB[rob_head].op = OP_NOP;
        ROB[rob_head].arch_dest = -1;
        ROB[rob_head].phys_dest = -1;
        ROB[rob_head].ready = 0;
        ROB[rob_head].value = 0;
        rob_head = (rob_head + 1) % 32;
        rob_count--;
    } else {
        if (index >= 0 && index < 32) {
            ROB[index].busy = 0;
            ROB[index].op = OP_NOP;
            ROB[index].arch_dest = -1;
            ROB[index].phys_dest = -1;
            ROB[index].ready = 0;
            ROB[index].value = 0;
        }
    }
}

void dump_instruction_queue() {
    printf("Instruction Queue (Count: %d, Head: %d, Tail: %d):\n", iq_count, iq_head, iq_tail);
    if (iq_count == 0) {
        printf("  [Empty]\n");
        return;
    }
    for (int i = 0; i < iq_count; i++) {
        int idx = (iq_head + i) % IQ_SIZE;
        printf("  [%2d] PC: 0x%04x | %s\n", idx, instruction_queue[idx].pc, instruction_queue[idx].instr);
    }
}

void dump_rat() {
    printf("Register Alias Table (RAT):\n");
    for (int i = 0; i < 32; i += 8) {
        printf("  ");
        for (int j = 0; j < 8; j++) {
            int reg = i + j;
            printf("x%d->P%d  ", reg, RAT[reg].phys_reg);
        }
        printf("\n");
    }
}

void dump_freelist() {
    printf("Free List (Count: %d):\n  ", fl_count);
    if (fl_count == 0) {
        printf("[Empty]\n");
        return;
    }
    for (int i = 0; i < fl_count; i++) {
        int idx = (fl_head + i) % FREE_LIST_SIZE;
        printf("P%d ", free_list[idx]);
    }
    printf("\n");
}

const char *opcode_to_string(opcode_t op) {
    switch(op) {
        case OP_ADD: return "add"; case OP_SUB: return "sub"; case OP_SLL: return "sll";
        case OP_SLT: return "slt"; case OP_SLTU: return "sltu"; case OP_XOR: return "xor";
        case OP_SRL: return "srl"; case OP_SRA: return "sra"; case OP_OR: return "or";
        case OP_AND: return "and"; case OP_ADDI: return "addi"; case OP_SLTI: return "slti";
        case OP_SLTIU: return "sltiu"; case OP_XORI: return "xori"; case OP_ORI: return "ori";
        case OP_ANDI: return "andi"; case OP_SLLI: return "slli"; case OP_SRLI: return "srli";
        case OP_SRAI: return "srai"; case OP_LW: return "lw"; case OP_LH: return "lh";
        case OP_LB: return "lb"; case OP_LHU: return "lhu"; case OP_LBU: return "lbu";
        case OP_SW: return "sw"; case OP_SH: return "sh"; case OP_SB: return "sb";
        case OP_BEQ: return "beq"; case OP_BNE: return "bne"; case OP_BLT: return "blt";
        case OP_BGE: return "bge"; case OP_BLTU: return "bltu"; case OP_BGEU: return "bgeu";
        case OP_LUI: return "lui"; case OP_AUIPC: return "auipc"; case OP_JAL: return "jal";
        case OP_JALR: return "jalr"; case OP_HALT: return "halt"; case OP_NOP: return "nop";
        default: return "unknown";
    }
}

void dump_rs() {
    printf("Reservation Stations (int_rs):\n");
    bool any_busy = false;
    for (int i = 0; i < 8; i++) {
        if (int_rs[i].busy) {
            any_busy = true;
            printf("  RS[%d]: op=%s dest=P%d | src1=P%d (ready=%d, val=%d) | src2=P%d (ready=%d, val=%d)\n",
                   i, opcode_to_string(int_rs[i].op), int_rs[i].dest_phys,
                   int_rs[i].src1_phys, int_rs[i].src1_ready, int_rs[i].src1_value,
                   int_rs[i].src2_phys, int_rs[i].src2_ready, int_rs[i].src2_value);
        }
    }
    if (!any_busy) {
        printf("  [All free]\n");
    }
}

void dump_rob() {
    printf("Reorder Buffer (ROB) Occupancy: %d/32 (Head: %d, Tail: %d):\n", rob_count, rob_head, rob_tail);
    if (rob_count == 0) {
        printf("  [Empty]\n");
        return;
    }
    for (int i = 0; i < rob_count; i++) {
        int idx = (rob_head + i) % 32;
        printf("  ROB[%2d]: op=%-6s | dest_arch=x%-2d | dest_phys=P%-2d | ready=%d | val=%d\n",
               idx, opcode_to_string(ROB[idx].op), ROB[idx].arch_dest, ROB[idx].phys_dest, ROB[idx].ready, ROB[idx].value);
    }
}

void dump_physical_registers() {
    printf("Physical Registers Status:\n");
    for (int i = 0; i < 64; i += 8) {
        printf("  ");
        for (int j = 0; j < 8; j++) {
            int reg = i + j;
            printf("P%02d:%-5d(%c) ", reg, physical_registers[reg].value, physical_registers[reg].ready ? 'R' : 'N');
        }
        printf("\n");
    }
}

void IF_stage(int instr_count) {
    if (pc_redirect) {
        pc = pc_next;
        pc_redirect = 0;
        iq_clear();
    }
    if (iq_is_full()) {
        printf("IF  : STALL (IQ full, PC frozen)\n");
        return;
    }
    if ((pc / 4) >= (uint32_t)instr_count) {
        return;
    }
    
    char *instr = instruction_memory[pc / 4];
    
    char trimmed_buf[MAX_LEN];
    strcpy(trimmed_buf, instr);
    char *trimmed = trimmed_buf;
    while(isspace((unsigned char)*trimmed)) trimmed++;
    if (strcmp(trimmed, "halt") == 0) halt_fetched = 1;

    iq_enqueue(pc, instr);
    pc += 4;
}

void parse_instruction(char *instr, ID_EX_t *latch) {
    char clean[MAX_LEN];
    strcpy(clean, instr);
    
    for (int i = 0; clean[i]; i++) {
        if (clean[i] == ',' || clean[i] == '(' || clean[i] == ')') clean[i] = ' ';
        
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
    
    else if (!strcmp(op, "addi")) { latch->op = OP_ADDI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "slli")) { latch->op = OP_SLLI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "srli")) { latch->op = OP_SRLI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "srai")) { latch->op = OP_SRAI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "slti")) { latch->op = OP_SLTI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "sltiu")){ latch->op = OP_SLTIU; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "xori")) { latch->op = OP_XORI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "ori")) { latch->op = OP_ORI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "andi")) { latch->op = OP_ANDI; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    
    else if (!strcmp(op, "lw")) { latch->op = OP_LW; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lh")) { latch->op = OP_LH; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lb")) { latch->op = OP_LB; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lhu")) { latch->op = OP_LHU; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "lbu")) { latch->op = OP_LBU; latch->rd = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    
    else if (!strcmp(op, "sw")) { latch->op = OP_SW; latch->rs2 = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "sh")) { latch->op = OP_SH; latch->rs2 = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    else if (!strcmp(op, "sb")) { latch->op = OP_SB; latch->rs2 = reg_index(a1); latch->imm = atoi(a2); latch->rs1 = reg_index(a3); }
    
    else if (!strcmp(op, "beq")) { latch->op = OP_BEQ; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bne")) { latch->op = OP_BNE; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "blt")) { latch->op = OP_BLT; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bge")) { latch->op = OP_BGE; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bltu")) { latch->op = OP_BLTU; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    else if (!strcmp(op, "bgeu")) { latch->op = OP_BGEU; latch->rs1 = reg_index(a1); latch->rs2 = reg_index(a2); latch->imm = atoi(a3); }
    
    else if (!strcmp(op, "lui")) { latch->op = OP_LUI; latch->rd = reg_index(a1); latch->imm = atoi(a2); }
    else if (!strcmp(op, "auipc")) { latch->op = OP_AUIPC; latch->rd = reg_index(a1); latch->imm = atoi(a2); }
    else if (!strcmp(op, "jal")) { latch->op = OP_JAL; latch->rd = reg_index(a1); latch->imm = atoi(a2); }
    else if (!strcmp(op, "jalr")) { latch->op = OP_JALR; latch->rd = reg_index(a1); latch->rs1 = reg_index(a2); latch->imm = atoi(a3); }
    
    else if (!strcmp(op, "halt")) { latch->op = OP_HALT; latch->valid = 1; latch->ctrl = (control_t){0}; return; }
}

void ID_stage() {
    ID_EX_new = (ID_EX_t){0};
    ID_EX_new.dest_phys = -1;
    ID_EX_new.old_dest_phys = -1;
    ID_EX_new.src1_phys = -1;
    ID_EX_new.src2_phys = -1;
    ID_EX_new.rs_index = -1;
    ID_EX_new.rob_index = -1;

    if (stall) { ID_EX_new.valid = 0; return; }
    if (iq_is_empty()) return;
    
    iq_entry_t entry = instruction_queue[iq_head];
    
    ID_EX_new.valid = 1;
    ID_EX_new.pc = entry.pc;
    parse_instruction(entry.instr, &ID_EX_new);

    if (ID_EX_old.valid && ID_EX_old.ctrl.MemRead && ID_EX_old.rd != 0 &&
        (ID_EX_old.rd == ID_EX_new.rs1 || ID_EX_old.rd == ID_EX_new.rs2))
    {
        stall = 1;
        ID_EX_new = (ID_EX_t){0};
        ID_EX_new.dest_phys = -1;
        ID_EX_new.old_dest_phys = -1;
        ID_EX_new.src1_phys = -1;
        ID_EX_new.src2_phys = -1;
        ID_EX_new.rs_index = -1;
        ID_EX_new.rob_index = -1;
        return;
    }

    iq_dequeue();

    if (ID_EX_new.op == OP_HALT) {
        return;
    }

    ID_EX_new.ctrl = control(ID_EX_new.op);
    ID_EX_new.v1 = reg_file[ID_EX_new.rs1];
    ID_EX_new.v2 = reg_file[ID_EX_new.rs2];

    int dest_phys = -1;
    int old_dest_phys = -1;
    if (ID_EX_new.ctrl.RegWrite && ID_EX_new.rd != 0) {
        dest_phys = allocate_phys_reg();
        if (dest_phys != -1) {
            old_dest_phys = get_phys_reg(ID_EX_new.rd);
            set_phys_reg(ID_EX_new.rd, dest_phys);
            physical_registers[dest_phys].ready = 0;
        }
    } else if (ID_EX_new.ctrl.RegWrite && ID_EX_new.rd == 0) {
        dest_phys = 0;
    }

    ID_EX_new.src1_phys = get_phys_reg(ID_EX_new.rs1);
    ID_EX_new.src2_phys = get_phys_reg(ID_EX_new.rs2);
    ID_EX_new.dest_phys = dest_phys;
    ID_EX_new.old_dest_phys = old_dest_phys;

    int rs_idx = -1;
    if (ID_EX_new.op != OP_NOP) {
        rs_idx = allocate_rs();
        if (rs_idx != -1) {
            int_rs[rs_idx].busy = 1;
            int_rs[rs_idx].op = ID_EX_new.op;
            int_rs[rs_idx].dest_phys = dest_phys;
            int_rs[rs_idx].src1_phys = ID_EX_new.src1_phys;
            int_rs[rs_idx].src2_phys = ID_EX_new.src2_phys;
            
            if (ID_EX_new.src1_phys >= 0 && ID_EX_new.src1_phys < PHYS_REGS) {
                int_rs[rs_idx].src1_ready = physical_registers[ID_EX_new.src1_phys].ready;
                int_rs[rs_idx].src1_value = physical_registers[ID_EX_new.src1_phys].value;
            } else {
                int_rs[rs_idx].src1_ready = 1;
                int_rs[rs_idx].src1_value = 0;
            }
            if (ID_EX_new.src2_phys >= 0 && ID_EX_new.src2_phys < PHYS_REGS) {
                int_rs[rs_idx].src2_ready = physical_registers[ID_EX_new.src2_phys].ready;
                int_rs[rs_idx].src2_value = physical_registers[ID_EX_new.src2_phys].value;
            } else {
                int_rs[rs_idx].src2_ready = 1;
                int_rs[rs_idx].src2_value = 0;
            }
        }
    }
    ID_EX_new.rs_index = rs_idx;

    int rob_idx = -1;
    if (ID_EX_new.op != OP_NOP) {
        rob_idx = allocate_rob_entry();
        if (rob_idx != -1) {
            ROB[rob_idx].busy = 1;
            ROB[rob_idx].op = ID_EX_new.op;
            ROB[rob_idx].arch_dest = ID_EX_new.rd;
            ROB[rob_idx].phys_dest = dest_phys;
            ROB[rob_idx].ready = 0;
            ROB[rob_idx].value = 0;
        }
    }
    ID_EX_new.rob_index = rob_idx;
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
    EX_MEM_new.dest_phys = -1;
    EX_MEM_new.old_dest_phys = -1;
    EX_MEM_new.rob_index = -1;

    if (!ID_EX_old.valid) { printf("EX  : BUBBLE\n"); return; }

    if (ID_EX_old.rs_index != -1) {
        free_rs(ID_EX_old.rs_index);
    }

    EX_MEM_new.valid = 1;
    EX_MEM_new.op = ID_EX_old.op;
    EX_MEM_new.ctrl = ID_EX_old.ctrl;
    EX_MEM_new.rd = ID_EX_old.rd;
    EX_MEM_new.dest_phys = ID_EX_old.dest_phys;
    EX_MEM_new.old_dest_phys = ID_EX_old.old_dest_phys;
    EX_MEM_new.rob_index = ID_EX_old.rob_index;

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
        iq_clear();
        ID_EX_new.valid = 0;
    }

    if (ID_EX_old.op == OP_HALT) {
        EX_MEM_new.valid = 1; EX_MEM_new.op = OP_HALT; EX_MEM_new.ctrl = (control_t){0};
        EX_MEM_new.dest_phys = -1;
        EX_MEM_new.old_dest_phys = -1;
        EX_MEM_new.rob_index = -1;
        return;
    }
    printf("EX  : ALU=%-5d | EX/MEM : rd=%d alu=%d\n", EX_MEM_new.alu, EX_MEM_new.rd, EX_MEM_new.alu);
}

void MEM_stage() {
    MEM_WB_new = (MEM_WB_t){0};
    MEM_WB_new.dest_phys = -1;
    MEM_WB_new.old_dest_phys = -1;
    MEM_WB_new.rob_index = -1;

    if (!EX_MEM_old.valid) return;
    if (EX_MEM_old.op == OP_HALT) { 
        MEM_WB_new.valid = 1; 
        MEM_WB_new.op = OP_HALT; 
        MEM_WB_new.dest_phys = EX_MEM_old.dest_phys;
        MEM_WB_new.old_dest_phys = EX_MEM_old.old_dest_phys;
        MEM_WB_new.rob_index = EX_MEM_old.rob_index;
        return; 
    }

    MEM_WB_new.valid = 1;
    MEM_WB_new.op = EX_MEM_old.op;
    MEM_WB_new.ctrl = EX_MEM_old.ctrl;
    MEM_WB_new.rd = EX_MEM_old.rd;
    MEM_WB_new.alu = EX_MEM_old.alu;
    MEM_WB_new.dest_phys = EX_MEM_old.dest_phys;
    MEM_WB_new.old_dest_phys = EX_MEM_old.old_dest_phys;
    MEM_WB_new.rob_index = EX_MEM_old.rob_index;

    uint32_t addr = EX_MEM_old.alu;
    uint32_t word_addr = addr / 4;
    uint32_t byte_offset = addr % 4;
    
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
    
    int32_t val = 0;
    if (MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd != 0) {
        val = MEM_WB_old.ctrl.MemToReg ? MEM_WB_old.mem_data : MEM_WB_old.alu;
        reg_file[MEM_WB_old.rd] = val;
        
        int p_reg = MEM_WB_old.dest_phys;
        if (p_reg >= 0 && p_reg < PHYS_REGS) {
            physical_registers[p_reg].value = val;
            physical_registers[p_reg].ready = 1;
        }
        
        if (MEM_WB_old.old_dest_phys != -1) {
            free_phys_reg(MEM_WB_old.old_dest_phys);
        }
    }
    
    if (MEM_WB_old.rob_index != -1) {
        int r_idx = MEM_WB_old.rob_index;
        ROB[r_idx].value = val;
        ROB[r_idx].ready = 1;
        free_rob_entry(r_idx);
    }
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
    
    iq_clear();
    init_physical_registers();
    init_rat();
    init_freelist();
    init_rs();
    init_rob();
    
    load_data_memory(data_file);

    FILE *ifp = fopen(inst_file, "r");
    if (!ifp) { printf("ERROR: Could not open instructions file '%s'\n", inst_file); return 1; }
    uint32_t n = 0;
    char line[MAX_LEN];
    while (n < IMEM_SIZE && fgets(line, MAX_LEN, ifp)) {
        char *start = line;
        while(isspace((unsigned char)*start)) start++;
        if (*start == '\0' || *start == '#' || (start[0] == '/' && start[1] == '/')) continue; 
        
        char *end = start + strlen(start) - 1;
        while(end > start && isspace((unsigned char)*end)) end--;
        end[1] = '\0';

        strcpy(instruction_memory[n], start);
        n++;
    }
    fclose(ifp);
    printf("--- Loaded %d instructions from %s ---\n", n, inst_file);

    while (!halt_done && (!iq_is_empty() || ID_EX_old.valid || EX_MEM_old.valid || MEM_WB_old.valid || (pc / 4 < n))) {
        cycle++;
        stall = 0;
        printf("\n--- CYCLE %u ---\n", cycle);
        WB_stage();
        MEM_stage();
        EX_stage();
        ID_stage();
        IF_stage(n);

        printf("\n================ Out-of-Order Infrastructure Status (Cycle %u) ================\n", cycle);
        dump_instruction_queue();
        printf("\n");
        dump_rat();
        printf("\n");
        dump_freelist();
        printf("\n");
        dump_rs();
        printf("\n");
        dump_rob();
        printf("\n");
        dump_physical_registers();
        printf("================================================================================\n");

        ID_EX_old = ID_EX_new;
        EX_MEM_old = EX_MEM_new;
        MEM_WB_old = MEM_WB_new;

        if (interactive && !halt_done && (!iq_is_empty() || ID_EX_old.valid || EX_MEM_old.valid || MEM_WB_old.valid || (pc / 4 < n))) {
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
