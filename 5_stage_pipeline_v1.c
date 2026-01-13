#include <stdio.h>
#include <string.h>

#define MAX_LEN 64
#define REG_COUNT 32
#define IMEM_SIZE 256
#define DMEM_SIZE 256

/* ================= ARCH STATE ================= */

int reg_file[REG_COUNT];
char instruction_memory[IMEM_SIZE][MAX_LEN];
int data_memory[DMEM_SIZE];
int pc = 0;

/* ================= OPCODES ================= */

typedef enum {
    OP_ADD, OP_ADDI, OP_LUI,
    OP_LW, OP_SW,
    OP_HALT, OP_NOP
} opcode_t;

/* ================= CONTROL ================= */

typedef struct {
    int RegWrite;
    int ALUSrc;
    int MemRead;
    int MemWrite;
    int MemToReg;
} control_t;

/* ================= PIPELINE REGISTERS ================= */

typedef struct {
    int valid, pc;
    char instr[MAX_LEN];
} IF_ID_t;

typedef struct {
    int valid, pc;
    int rs1, rs2, rd, imm;
    int v1, v2;
    opcode_t op;
    control_t ctrl;
} ID_EX_t;

typedef struct {
    int valid;
    int alu;
    int rd;
    int store_val;
    opcode_t op;
    control_t ctrl;
} EX_MEM_t;

typedef struct {
    int valid;
    int alu;
    int mem_data;
    int rd;
    opcode_t op;
    control_t ctrl;
} MEM_WB_t;

/* ================= LATCHED PIPELINE ================= */

IF_ID_t  IF_ID = {0};
ID_EX_t  ID_EX_old = {0}, ID_EX_new = {0};
EX_MEM_t EX_MEM_old = {0}, EX_MEM_new = {0};
MEM_WB_t MEM_WB_old = {0}, MEM_WB_new = {0};

int halt_fetched = 0;
int halt_done = 0;
int cycle = 0;

/* ================= CONTROL LOGIC ================= */

control_t control(opcode_t op) {
    control_t c = {0};

    switch (op) {
        case OP_ADD:
            c.RegWrite = 1;
            break;
        case OP_ADDI:
            c.RegWrite = 1;
            c.ALUSrc = 1;
            break;
        case OP_LUI:
            c.RegWrite = 1;
            break;
        case OP_LW:
            c.RegWrite = 1;
            c.ALUSrc = 1;
            c.MemRead = 1;
            c.MemToReg = 1;
            break;
        case OP_SW:
            c.ALUSrc = 1;
            c.MemWrite = 1;
            break;
        default:
            break;
    }
    return c;
}

/* ================= FORWARDING ================= */

int forward_ex(int rs, int val) {
    if (rs == 0) return val;

    if (EX_MEM_old.valid && EX_MEM_old.ctrl.RegWrite &&
        EX_MEM_old.rd == rs) {
        printf("      [FWD] EX/MEM : x%d = %d\n", rs, EX_MEM_old.alu);
        return EX_MEM_old.alu;
    }

    if (MEM_WB_old.valid && MEM_WB_old.ctrl.RegWrite &&
        MEM_WB_old.rd == rs) {
        int v = MEM_WB_old.ctrl.MemToReg ?
                MEM_WB_old.mem_data :
                MEM_WB_old.alu;
        printf("      [FWD] MEM/WB : x%d = %d\n", rs, v);
        return v;
    }

    return val;
}

/* ================= IF ================= */

void IF_stage(int n) {
    if (halt_fetched || pc/4 >= n) {
        IF_ID.valid = 0;
        printf("IF  : IDLE | IF/ID : invalid\n");
        return;
    }

    IF_ID.valid = 1;
    IF_ID.pc = pc;
    strcpy(IF_ID.instr, instruction_memory[pc/4]);
    pc += 4;

    if (!strncmp(IF_ID.instr,"halt",4)) halt_fetched = 1;

    printf("IF  : PC=%-3d %-18s | IF/ID : PC=%-3d instr=%s",
           IF_ID.pc, IF_ID.instr, IF_ID.pc, IF_ID.instr);
}

/* ================= ID ================= */

void ID_stage() {
    if (!IF_ID.valid) {
        ID_EX_new.valid = 0;
        printf("ID  : IDLE | ID/EX : invalid\n");
        return;
    }

    char op[8];
    ID_EX_new = (ID_EX_t){0};
    ID_EX_new.valid = 1;
    ID_EX_new.pc = IF_ID.pc;

    sscanf(IF_ID.instr,"%s",op);

    if (!strcmp(op,"lui")) {
        ID_EX_new.op = OP_LUI;
        sscanf(IF_ID.instr,"%*s x%d,%d",&ID_EX_new.rd,&ID_EX_new.imm);
    }
    else if (!strcmp(op,"addi")) {
        ID_EX_new.op = OP_ADDI;
        sscanf(IF_ID.instr,"%*s x%d,x%d,%d",
               &ID_EX_new.rd,&ID_EX_new.rs1,&ID_EX_new.imm);
    }
    else if (!strcmp(op,"add")) {
        ID_EX_new.op = OP_ADD;
        sscanf(IF_ID.instr,"%*s x%d,x%d,x%d",
               &ID_EX_new.rd,&ID_EX_new.rs1,&ID_EX_new.rs2);
    }
    else if (!strcmp(op,"lw")) {
        ID_EX_new.op = OP_LW;
        sscanf(IF_ID.instr,"%*s x%d,%d(x%d)",
               &ID_EX_new.rd,&ID_EX_new.imm,&ID_EX_new.rs1);
    }
    else if (!strcmp(op,"sw")) {
        ID_EX_new.op = OP_SW;
        sscanf(IF_ID.instr,"%*s x%d,%d(x%d)",
               &ID_EX_new.rs2,&ID_EX_new.imm,&ID_EX_new.rs1);
    }
    else if (!strcmp(op,"halt")) {
        ID_EX_new.op = OP_HALT;
    }
    else {
        ID_EX_new.op = OP_NOP;
    }

    ID_EX_new.ctrl = control(ID_EX_new.op);
    ID_EX_new.v1 = reg_file[ID_EX_new.rs1];
    ID_EX_new.v2 = reg_file[ID_EX_new.rs2];

    printf("ID  : decode op=%d | ID/EX : rd=%d rs1=%d rs2=%d imm=%d\n",
           ID_EX_new.op,
           ID_EX_new.rd,ID_EX_new.rs1,ID_EX_new.rs2,ID_EX_new.imm);
}

/* ================= EX ================= */

void EX_stage() {
    if (!ID_EX_old.valid) {
        EX_MEM_new.valid = 0;
        printf("EX  : BUBBLE | EX/MEM : invalid\n");
        return;
    }

    EX_MEM_new.valid = 1;
    EX_MEM_new.op = ID_EX_old.op;
    EX_MEM_new.ctrl = ID_EX_old.ctrl;
    EX_MEM_new.rd = ID_EX_old.rd;

    int a = forward_ex(ID_EX_old.rs1, ID_EX_old.v1);
    int b = ID_EX_old.ctrl.ALUSrc ?
            ID_EX_old.imm :
            forward_ex(ID_EX_old.rs2, ID_EX_old.v2);

    if (ID_EX_old.op == OP_LUI)
        EX_MEM_new.alu = ID_EX_old.imm << 12;
    else
        EX_MEM_new.alu = a + b;

    EX_MEM_new.store_val =
        forward_ex(ID_EX_old.rs2, ID_EX_old.v2);

    printf("EX  : ALU=%d | EX/MEM : rd=%d alu=%d\n",
           EX_MEM_new.alu,EX_MEM_new.rd,EX_MEM_new.alu);
}

/* ================= MEM ================= */

void MEM_stage() {
    if (!EX_MEM_old.valid) {
        MEM_WB_new.valid = 0;
        printf("MEM : IDLE | MEM/WB : invalid\n");
        return;
    }

    MEM_WB_new.valid = 1;
    MEM_WB_new.op = EX_MEM_old.op;
    MEM_WB_new.ctrl = EX_MEM_old.ctrl;
    MEM_WB_new.rd = EX_MEM_old.rd;
    MEM_WB_new.alu = EX_MEM_old.alu;

    if (EX_MEM_old.ctrl.MemRead) {
        MEM_WB_new.mem_data =
            data_memory[EX_MEM_old.alu / 4];
        printf("MEM : LOAD mem[%d] = %d\n",
               EX_MEM_old.alu,
               MEM_WB_new.mem_data);
    }

    if (EX_MEM_old.ctrl.MemWrite) {
        data_memory[EX_MEM_old.alu / 4] =
            EX_MEM_old.store_val;
        printf("MEM : STORE mem[%d] = %d\n",
               EX_MEM_old.alu,
               EX_MEM_old.store_val);
    }
}

/* ================= WB ================= */

void WB_stage() {
    if (!MEM_WB_old.valid) {
        printf("WB  : IDLE\n");
        return;
    }

    if (MEM_WB_old.op == OP_HALT) {
        halt_done = 1;
        printf("WB  : HALT\n");
        return;
    }

    if (MEM_WB_old.ctrl.RegWrite && MEM_WB_old.rd) {
        int val = MEM_WB_old.ctrl.MemToReg ?
                  MEM_WB_old.mem_data :
                  MEM_WB_old.alu;
        reg_file[MEM_WB_old.rd] = val;
        printf("WB  : WRITE x%d <= %d\n",
               MEM_WB_old.rd,val);
    }
}

/* ================= MAIN ================= */

int main() {
    FILE *fp = fopen("instructions.txt","r");
    int n=0;
    while (fgets(instruction_memory[n],MAX_LEN,fp)) n++;
    fclose(fp);

    memset(reg_file,0,sizeof(reg_file));
    memset(data_memory,0,sizeof(data_memory));

    while (!halt_done || IF_ID.valid || ID_EX_old.valid ||
           EX_MEM_old.valid || MEM_WB_old.valid) {

        cycle++;
        printf("\n============= CYCLE %d =============\n",cycle);

        WB_stage();
        MEM_stage();
        EX_stage();
        ID_stage();
        IF_stage(n);

        ID_EX_old  = ID_EX_new;
        EX_MEM_old = EX_MEM_new;
        MEM_WB_old = MEM_WB_new;
    }

    printf("\n=========== PROGRAM COMPLETE ===========\n");
    return 0;
}
