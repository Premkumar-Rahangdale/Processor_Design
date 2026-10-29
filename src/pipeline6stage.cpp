#include "../include/pipeline6stage.hpp"
#include <iostream>


int Pipeline6stage::field(const std::string& s, int start, int len) {
    int v = 0;
    for (int i = 0; i < len; i++) v = (v << 1) | (s[start + i] - '0');
    return v;
}

int Pipeline6stage::signExtend(int v, int bits) {
    int m = 1 << (bits - 1);
    return (v ^ m) - m;
}

void Pipeline6stage::fault(const std::string& msg) {
    std::cerr << "[pipeline] " << msg << "\n";
    halted = true;
}

// Sequential model: each instruction goes through all six stages before the
// next one is fetched. Because of that there are no hazards and no flushes yet.
void Pipeline6stage::run(MachineState& machine) {
    machine.reset();          // does not touch instrMemory / labels
    halted = false;
    long executed = 0;

    while (!halted && machine.pc >= 0 && machine.pc < (int)machine.instrMemory.size() && executed < kMaxInstructions)
    {
        if (!IF_Stage(machine)) continue;   // label line, skip

        ID_Stage(machine);   if (halted) break;
        RR_Stage(machine);
        EX_Stage(machine);   if (halted) break;
        MEM_Stage(machine);  if (halted) break;
        RW_Stage(machine);

        executed++;
    }

    if (executed >= kMaxInstructions)
        std::cerr << "[pipeline] stopped: instruction limit reached\n";

    if (trace) {
        std::cout << "---- done: " << executed << " instructions ----\n";
        for (int i = 0; i < (int)machine.Reg.size(); i++)
            if (machine.Reg[i] != 0)
                std::cout << "r" << i << " = " << machine.Reg[i] << "\n";
        for (int i = 0; i < (int)machine.memory.size(); i++)
            if (machine.memory[i] != 0)
                std::cout << "mem[" << i << "] = " << machine.memory[i] << "\n";
    }
}


// IF: fetch instrMemory[pc], advance pc.
bool Pipeline6stage::IF_Stage(MachineState& machine) {
    const std::string& word = machine.instrMemory[machine.pc];

    if (!word.empty() && word[0] == '.') {
        machine.pc++;
        return false;
    }
    if (word.size() != 32) {
        fault("bad instruction word at index " + std::to_string(machine.pc));
        return false;
    }

    if_id = Latch{};
    if_id.valid = true;
    if_id.pc = machine.pc;
    if_id.raw = word;
    machine.pc++;                   // EX overwrites this if a branch is taken
    return true;
}

// ID: run the control unit, split the instruction into fields.
void Pipeline6stage::ID_Stage(MachineState& machine) {
    id_rr = if_id;
    Latch& L = id_rr;

    generatecontrolsignals(L.raw.substr(0, 6));   // opcode + I bit
    L.sig = signals;

    if (!L.sig.isKnown()) {
        fault("illegal opcode at index " + std::to_string(L.pc));
        return;
    }

    L.rd     = field(L.raw, 6, 5);
    L.rs1    = field(L.raw, 11, 5);
    L.rs2    = field(L.raw, 16, 5);
    L.imm    = signExtend(field(L.raw, 16, 16), 16);
    L.offset = signExtend(field(L.raw, 5, 27), 27);   // branches only
}

// RR: read the register file.
void Pipeline6stage::RR_Stage(MachineState& machine) {
    rr_ex = id_rr;
    Latch& L = rr_ex;
    const controlsignals& s = L.sig;

    auto reg = [&machine](int i) { return i == 0 ? 0 : machine.Reg[i]; };   // R0 always reads 0

    if (s.readsRs1())      L.opA = reg(L.rs1);
    if (s.isRet)           L.opA = reg(kRa);            // return address
    if (s.usesOperandB())  L.opB = s.isImm ? L.imm : reg(L.rs2);
    if (s.isSt)            L.storeVal = reg(L.rd);      // st: rd is the source
}

// EX: ALU work, effective address, branch decision.
void Pipeline6stage::EX_Stage(MachineState& machine) {
    ex_mem = rr_ex;
    Latch& L = ex_mem;
    const controlsignals& s = L.sig;

    if (s.isCall) {
        L.aluResult = L.pc + 1;                              // link value for ra
    } else if (s.isLd || s.isSt || s.writesReg() || s.isCmp) {
        ALUResult r = alu_unit.execute(s, L.opA, L.opB);     // ld/st: address = base + offset
        if (r.divByZero) {
            fault("division by zero at index " + std::to_string(L.pc));
            return;
        }
        L.aluResult = static_cast<int>(r.value);
        if (r.updateFlags) {                                 // only cmp sets the flags
            machine.zero = r.zero;  machine.neg = r.neg;
            machine.over = r.over;  machine.carry = r.carry;
        }
    }

    bool taken = false;
    int target = 0;
    if (s.isB || s.isCall)  { taken = true;target = L.pc + L.offset; }
    else if (s.isBeq)       { taken = machine.zero;target = L.pc + L.offset; }
    else if (s.isBgt)       { taken = !machine.zero && (machine.neg == machine.over);target = L.pc + L.offset; }
    else if (s.isBsm)       { taken = (machine.neg != machine.over);target = L.pc + L.offset; }
    else if (s.isRet)       { taken = true;target = L.opA; }

    L.branchTaken = taken;
    L.branchTarget = target;
    if (taken) machine.pc = target;
}

// MEM: data memory access (word-addressed).
void Pipeline6stage::MEM_Stage(MachineState& machine) {
    mem_rw = ex_mem;
    Latch& L = mem_rw;
    const controlsignals& s = L.sig;

    if (s.isLd || s.isSt) {
        int addr = L.aluResult;
        if (addr < 0 || addr >= (int)machine.memory.size()) {
            fault("memory address out of range: " + std::to_string(addr));
            return;
        }
        if (s.isLd) L.memData = machine.memory[addr];
        if (s.isSt) machine.memory[addr] = L.storeVal;
    }
    L.writeData = s.isLd ? L.memData : L.aluResult;
}

// RW: write the result back to the register file.
void Pipeline6stage::RW_Stage(MachineState& m) {
    const Latch& L = mem_rw;
    const controlsignals& s = L.sig;

    int dest = s.isCall ? kRa : L.rd;
    if (s.writesReg() && dest != 0) m.Reg[dest] = L.writeData;   // writes to R0 are ignored

    if (trace) {
        std::cout << "pc=" << L.pc << "  " << L.raw << "  ";
        if (s.writesReg()) std::cout << "r" << dest << " <- " << L.writeData;
        else               std::cout << "(no writeback)";
        if (L.branchTaken) std::cout << "  [jump to " << L.branchTarget << "]";
        std::cout << "\n";
    }
}