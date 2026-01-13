#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_INSTRUCTIONS 1024
#define MAX_LEN 64

// ================= REGISTER FILE =================
int reg_file[32] = {0};

// ================= ALU OPS =================
#define ALU_ADD 0
#define ALU_SUB 1
#define ALU_AND 2
#define ALU_OR  3
#define ALU_XOR 4

// ================= INSTRUCTION MEMORY =================
char instruction_memory[MAX_INSTRUCTIONS][MAX_LEN];
int instruction_count = 0;
int PC = 0;

// ================= PIPELINE REGISTERS =================
typedef struct {
    int pc;
    char instruction[MAX_LEN];
} IF_ID_Reg;

typedef struct {
    int pc;
    int rs1;
    int rs2;
    int rd;
    int alu_op;
} ID_EX_Reg;

typedef struct {
    int pc;
    int rd;
    int alu_result;
} EX_MEM_Reg;

typedef struct {
    int pc;
    int rd;
    int write_data;
} MEM_WB_Reg;

IF_ID_Reg IF_ID;
ID_EX_Reg ID_EX;
EX_MEM_Reg EX_MEM;
MEM_WB_Reg MEM_WB;

// ================= HELPER FUNCTIONS =================
void load_instructions(const char *filename) {
    FILE *fp = fopen("instructions.txt", "r");
    if (!fp) {
        printf("Cannot open instruction file\n");
        exit(1);
    }

    while (fgets(instruction_memory[instruction_count], MAX_LEN, fp)) {
        instruction_memory[instruction_count]
            [strcspn(instruction_memory[instruction_count], "\n")] = 0;
        instruction_count++;
    }
    fclose(fp);
}

int parse_reg(const char *r) {
    if (r[0] == 'x' || r[0] == 'X')
        return atoi(r + 1);
    return atoi(r);
}

int decode_alu_op(const char *op) {
    if (!strcasecmp(op, "ADD")) return ALU_ADD;
    if (!strcasecmp(op, "SUB")) return ALU_SUB;
    if (!strcasecmp(op, "AND")) return ALU_AND;
    if (!strcasecmp(op, "OR"))  return ALU_OR;
    if (!strcasecmp(op, "XOR")) return ALU_XOR;
    return ALU_ADD;
}

int alu_compute(int op, int a, int b) {
    switch (op) {
        case ALU_ADD: return a + b;
        case ALU_SUB: return a - b;
        case ALU_AND: return a & b;
        case ALU_OR:  return a | b;
        case ALU_XOR: return a ^ b;
        default: return 0;
    }
}

// ================= PIPELINE STAGES =================

// -------- IF --------
void instruction_fetch() {
    if (PC / 4 >= instruction_count) {
        strcpy(IF_ID.instruction, "NOP");
        IF_ID.pc = PC;
    } else {
        strcpy(IF_ID.instruction, instruction_memory[PC / 4]);
        IF_ID.pc = PC;
        PC += 4;
    }

    printf("[IF ] PC=%d  INSTR=\"%s\"\n",
           IF_ID.pc, IF_ID.instruction);
}

// -------- ID --------
void instruction_decode() {
    char op[8], rd_s[8], rs1_s[8], rs2_s[8];

    if (!strcmp(IF_ID.instruction, "NOP")) {
        ID_EX.rd = ID_EX.rs1 = ID_EX.rs2 = 0;
        ID_EX.alu_op = ALU_ADD;
        ID_EX.pc = IF_ID.pc;

        printf("[ID ] NOP\n");
        return;
    }

    int n = sscanf(IF_ID.instruction,
                   "%7s %7[^,], %7[^,], %7s",
                   op, rd_s, rs1_s, rs2_s);

    if (n != 4) {
        ID_EX.rd = ID_EX.rs1 = ID_EX.rs2 = 0;
        ID_EX.alu_op = ALU_ADD;
        return;
    }

    ID_EX.pc = IF_ID.pc;
    ID_EX.rd = parse_reg(rd_s);
    ID_EX.rs1 = parse_reg(rs1_s);
    ID_EX.rs2 = parse_reg(rs2_s);
    ID_EX.alu_op = decode_alu_op(op);

    printf("[ID ] pc=%d  rd=x%d rs1=x%d rs2=x%d  "
           "rs1_val=%d rs2_val=%d\n",
           ID_EX.pc,
           ID_EX.rd, ID_EX.rs1, ID_EX.rs2,
           reg_file[ID_EX.rs1],
           reg_file[ID_EX.rs2]);
}

// -------- EX --------
void execute() {
    int a = reg_file[ID_EX.rs1];
    int b = reg_file[ID_EX.rs2];

    EX_MEM.alu_result = alu_compute(ID_EX.alu_op, a, b);
    EX_MEM.rd = ID_EX.rd;
    EX_MEM.pc = ID_EX.pc;

    printf("[EX ] pc=%d  ALU_OUT=%d  rd=x%d\n",
           EX_MEM.pc, EX_MEM.alu_result, EX_MEM.rd);
}

// -------- MEM --------
void memory_access() {
    MEM_WB.write_data = EX_MEM.alu_result;
    MEM_WB.rd = EX_MEM.rd;
    MEM_WB.pc = EX_MEM.pc;

    printf("[MEM] pc=%d  WRITE_DATA=%d\n",
           MEM_WB.pc, MEM_WB.write_data);
}

// -------- WB --------
void write_back() {
    if (MEM_WB.rd != 0) {
        reg_file[MEM_WB.rd] = MEM_WB.write_data;
        printf("[WB ] pc=%d  x%d <= %d\n",
               MEM_WB.pc, MEM_WB.rd, MEM_WB.write_data);
    } else {
        printf("[WB ] pc=%d  NO WRITE\n", MEM_WB.pc);
    }
}

// ================= MAIN =================
int main() {
    load_instructions("instructions.txt");

    
    reg_file[1] = 10;
    reg_file[2] = 20;

    int cycles = instruction_count + 4;

    for (int i = 0; i < cycles; i++) {
        printf("\n--- Cycle %d ---\n", i + 1);
        
        instruction_fetch();
        instruction_decode();
        execute();
        memory_access();
        write_back();
    }

    printf("\n=== REGISTER FILE ===\n");
    for (int i = 0; i < 8; i++)
        printf("x%d = %d\n", i, reg_file[i]);

    return 0;
}
