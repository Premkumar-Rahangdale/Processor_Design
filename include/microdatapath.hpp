#pragma once
#include <cstdint>
#include <string>
#include "MachineState.hpp"
#include "alu.hpp"
#include "microinstr.hpp"

// ===========================================================================
//  Micro-programmed data path (book section 8.5 - 8.7)
//
//  Units: instruction memory + decode unit, register file, ALU, data memory,
//  exception unit. They talk to each other only through the micro-registers
//  and the shared bus. One call to execute() is one cycle of the data path.
//
//  The architectural state (registers, memory, flags, pc, instruction memory)
//  stays in MachineState; the data path never copies it. pc is the machine's
//  pc, an index into instrMemory (+1 per instruction).
// ===========================================================================
class MicroDatapath {
public:
    explicit MicroDatapath(MachineState& machine);

    void reset();                                   // clears the micro-registers and unit state

    // One cycle: (1) the register selected by the out signals drives the write
    // bus, (2) the transfer multiplexer picks bus / bus + imm / imm, (3) every
    // register with its in signal loads that value, (4) the unit named by args
    // performs its operation using the freshly loaded registers.
    // Returns isMBranch (write bus == micro-immediate), used by mbeq.
    bool execute(const MicroSignals& s);

    // Switch unit input: the 5-bit opcode held in ir
    uint32_t opcode() const { return regs_[static_cast<int>(MReg::IR)] >> 27; }

    uint32_t reg(MReg r) const;                     // value of a micro-register (for inspection)

    bool programEnded() const { return ended_; }    // pc left instruction memory (normal end)
    bool halted() const { return halted_; }         // exception unit executed <halt>

    // Exceptions: raised by the units (ALU, memory) or the switch unit
    void raise(uint32_t cause);
    bool faultPending() const { return fault_; }
    void clearFault() { fault_ = false; }
    uint32_t excCause() const { return regs_[static_cast<int>(MReg::EXC_CAUSE)]; }

private:
    MachineState& m_;
    ALU alu_;
    uint32_t regs_[kNumMRegs] = {};                 // pc is not stored here, it is m_.pc
    bool decodePending_ = false;                    // ir was loaded last cycle
    bool ended_ = false, halted_ = false, fault_ = false;

    uint32_t read(MReg r) const;
    void write(MReg r, uint32_t v);

    void loadIR();                                  // instruction memory unit
    void latchDecode();                             // decode unit
    void runUnit(uint16_t args);
    void aluOp(uint16_t op);
};
