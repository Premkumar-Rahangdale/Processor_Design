#pragma once
#include <string>
#include <vector>
#include "MachineState.hpp"
#include "controlunit.hpp"
#include "alu.hpp"

// Each pipeline register (IF/ID, ID/RR, RR/EX, EX/MEM, MEM/RW) is one Latch.
struct Latch {
    bool valid = false;
    int  pc = 0;                 // index of this instruction in instrMemory
    std::string raw;             // 32-char binary string

    controlsignals sig;          // filled in ID

    int rd = 0, rs1 = 0, rs2 = 0;
    int imm = 0;                 // sign-extended 16-bit immediate
    int offset = 0;              // sign-extended 27-bit branch offset

    int opA = 0, opB = 0;        // operands read in RR
    int storeVal = 0;            // value to store (st), read in RR

    int aluResult = 0;           // EX: ALU result / ld-st address / call link
    int memData = 0;             // MEM: loaded value
    int writeData = 0;           // MEM: final value to write back

    bool branchTaken = false;    // EX
    int  branchTarget = 0;       // EX
};

class Pipeline6stage : public controlunit
{
public:
    bool trace = true;           // print one line per instruction in RW
    void run(MachineState& machine);

private:
    static constexpr int  kRa = 1;                 // "ra" is r1 in your assembler
    static constexpr long kMaxInstructions = 1000000;  // runaway-loop guard

    Latch if_id, id_rr, rr_ex, ex_mem, mem_rw;
    bool halted = false;
    ALU alu_unit;

    // returns false if the fetched entry was a label (nothing to execute)
    bool IF_Stage(MachineState& machine);
    void ID_Stage(MachineState& machine);
    void RR_Stage(MachineState& machine);
    void EX_Stage(MachineState& machine);
    void MEM_Stage(MachineState& machine);
    void RW_Stage(MachineState& machine);


    static int field(const std::string& s, int start, int len);
    static int signExtend(int v, int bits);
    void fault(const std::string& msg);
};