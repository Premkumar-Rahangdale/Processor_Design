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
    bool trace = false;           // print one line per instruction in RW
    void run(MachineState& machine);

    // ---- step-wise execution (used by the debugger) -------------------------
    // The same six stages as run(); only the loop around them is different.
    //
    // begin()   : fresh start. machine.reset() (registers, memory, flags, pc),
    //             instrMemory / labels are kept.
    // step()    : executes ONE instruction, starting at machine.pc (label lines
    //             are skipped). Returns false if nothing was executed because the
    //             program is finished or a fault stopped it.
    // stepN()   : executes up to n instructions from machine.pc and returns how
    //             many were really executed. If executedPcs is given, the pc
    //             (index in instrMemory) of every executed instruction is appended.
    // nextPc()  : index of the instruction step() would execute next, -1 if none.
    // finished(): true when step() has nothing left to execute.
    void begin(MachineState& machine);
    bool step(MachineState& machine);
    int  stepN(MachineState& machine, int n, std::vector<int>* executedPcs = nullptr);
    int  nextPc(const MachineState& machine) const;
    bool finished(const MachineState& machine) const;
    bool faulted() const { return halted; }              // stopped by a fault
    long instructionsExecuted() const { return executed; }

private:
    static constexpr int  kRa = 1;                 // "ra" is r1 in your assembler
    static constexpr long kMaxInstructions = 1000000;  // runaway-loop guard

    Latch if_id, id_rr, rr_ex, ex_mem, mem_rw;
    bool halted = false;
    long executed = 0;           // instructions completed since begin()
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
