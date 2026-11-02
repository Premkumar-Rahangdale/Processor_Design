#include "../include/microdatapath.hpp"
#include <stdexcept>

namespace {
inline int mdIdx(MReg r) { return static_cast<int>(r); }

int mdSext(uint32_t v, int bits) {
    const uint32_t m = 1u << (bits - 1);
    return static_cast<int>((v ^ m) - m);
}
}  // namespace

MicroDatapath::MicroDatapath(MachineState& machine) : m_(machine) { reset(); }

void MicroDatapath::reset() {
    for (int i = 0; i < kNumMRegs; i++) regs_[i] = 0;
    decodePending_ = false;
    ended_ = halted_ = fault_ = false;
}

uint32_t MicroDatapath::read(MReg r) const {
    if (r == MReg::PC) return static_cast<uint32_t>(m_.pc);
    return regs_[mdIdx(r)];
}

void MicroDatapath::write(MReg r, uint32_t v) {
    if (r == MReg::PC) m_.pc = static_cast<int>(v);
    else regs_[mdIdx(r)] = v;
}

uint32_t MicroDatapath::reg(MReg r) const { return read(r); }

void MicroDatapath::raise(uint32_t cause) {
    if (!fault_) {                                  // the first fault of a cycle wins
        fault_ = true;
        regs_[mdIdx(MReg::EXC_CAUSE)] = cause;
    }
}

// ---------------------------------------------------------------------------
//  One cycle
// ---------------------------------------------------------------------------
bool MicroDatapath::execute(const MicroSignals& s) {
    // 1. write bus: at most one register may drive it
    int32_t bus = 0;
    int drivers = 0;
    for (int r = 0; r < kNumMRegs; r++) {
        if ((s.out >> r) & 1u) {
            bus = static_cast<int32_t>(read(static_cast<MReg>(r)));
            drivers++;
        }
    }
    if (drivers > 1) throw std::logic_error("write-bus conflict: two registers drive the bus");

    const bool isMBranch = (bus == s.imm);          // compared with the micro-immediate

    // 2. transfer multiplexer
    int32_t xfer = bus;
    if (s.xfer == kXferAdder) xfer = static_cast<int32_t>(static_cast<uint32_t>(bus) + static_cast<uint32_t>(s.imm));
    else if (s.xfer == kXferImm) xfer = s.imm;

    // 3. read bus: every register with its in signal loads
    const bool decodeNow = decodePending_;          // ir was loaded in the previous cycle (mdecode cycle)
    decodePending_ = false;
    for (int r = 0; r < kNumMRegs; r++) {
        if (!((s.in >> r) & 1u)) continue;
        const MReg reg = static_cast<MReg>(r);
        if (reg == MReg::IR) {
            loadIR();
            decodePending_ = !ended_;
        } else {
            write(reg, static_cast<uint32_t>(xfer));
        }
    }

    // 4. functional unit selected by args
    runUnit(s.args);

    // 5. decode registers are populated during the mdecode cycle
    if (decodeNow) latchDecode();

    return isMBranch;
}

// ---------------------------------------------------------------------------
//  Instruction memory unit: ir <- [pc]
//  Label entries in instrMemory are not instructions; like the IF stage of the
//  pipeline this unit steps over them at no cost. A malformed entry reads as
//  0, whose opcode (0) is illegal.
// ---------------------------------------------------------------------------
void MicroDatapath::loadIR() {
    const int size = static_cast<int>(m_.instrMemory.size());
    int p = m_.pc;
    while (p >= 0 && p < size && !m_.instrMemory[p].empty() && m_.instrMemory[p][0] == '.') p++;
    m_.pc = p;
    if (p < 0 || p >= size) {                       // pc left the program: normal end
        ended_ = true;
        return;
    }
    const std::string& w = m_.instrMemory[p];
    uint32_t word = 0;
    if (w.size() == 32 && w.find_first_not_of("01") == std::string::npos)
        word = static_cast<uint32_t>(std::stoul(w, nullptr, 2));
    regs_[mdIdx(MReg::IR)] = word;
}

// ---------------------------------------------------------------------------
//  Decode unit (RISC201 instruction layout; string index 0 = bit 31)
//     opcode 31:27   I 26   rd 25:21   rs1 20:16   rs2 15:11
//     imm 15:0 (sign-extended)   offset 26:0 (sign-extended)
// ---------------------------------------------------------------------------
void MicroDatapath::latchDecode() {
    const uint32_t w = regs_[mdIdx(MReg::IR)];
    regs_[mdIdx(MReg::I)]   = (w >> 26) & 1u;
    regs_[mdIdx(MReg::RD)]  = (w >> 21) & 31u;
    regs_[mdIdx(MReg::RS1)] = (w >> 16) & 31u;
    regs_[mdIdx(MReg::RS2)] = (w >> 11) & 31u;
    regs_[mdIdx(MReg::IMMX)] = static_cast<uint32_t>(mdSext(w & 0xFFFFu, 16));
    const int offset = mdSext(w & 0x7FFFFFFu, 27);
    regs_[mdIdx(MReg::BRANCH_TARGET)] = static_cast<uint32_t>(m_.pc + offset);
}

// ---------------------------------------------------------------------------
//  args: unit id (3 bits) | unit opcode (7 bits); 0 = nop
// ---------------------------------------------------------------------------
void MicroDatapath::runUnit(uint16_t args) {
    if (args == 0) return;
    const unsigned unit = args >> 7;
    const unsigned op = args & 0x7Fu;

    switch (unit) {
        case margs::UNIT_ALU:
            aluOp(args);
            break;

        case margs::UNIT_REGFILE: {                 // register file; R0 is ground
            const unsigned idx = regs_[mdIdx(MReg::REG_SRC)] & 31u;
            if (op == 1) {                          // <read>
                regs_[mdIdx(MReg::REG_VAL)] = (idx == 0) ? 0u : static_cast<uint32_t>(m_.Reg[idx]);
            } else if (op == 2) {                   // <write>
                if (idx != 0) m_.Reg[idx] = static_cast<int>(regs_[mdIdx(MReg::REG_DATA)]);
            } else {
                throw std::logic_error("unknown register-file operation");
            }
            break;
        }

        case margs::UNIT_MEM: {                     // data memory, word addressed
            const int32_t addr = static_cast<int32_t>(regs_[mdIdx(MReg::MAR)]);
            if (op != 1 && op != 2) throw std::logic_error("unknown memory operation");
            if (addr < 0 || addr >= static_cast<int32_t>(m_.memory.size())) {
                raise(mcause::MEM_ADDRESS);         // the access does not happen
                break;
            }
            if (op == 1) regs_[mdIdx(MReg::LD_RESULT)] = static_cast<uint32_t>(m_.memory[addr]);   // <load>
            else m_.memory[addr] = static_cast<int>(regs_[mdIdx(MReg::MDR)]);                       // <store>
            break;
        }

        case margs::UNIT_EXC:
            if (op == 1) halted_ = true;            // <halt>
            else throw std::logic_error("unknown exception-unit operation");
            break;

        default:
            throw std::logic_error("args name a unit that does not exist");
    }
}

// ---------------------------------------------------------------------------
//  ALU unit: wraps the existing ALU class. Only cmp updates the flags; the
//  derived GT / LT flags are produced together with them.
// ---------------------------------------------------------------------------
void MicroDatapath::aluOp(uint16_t args) {
    controlsignals cs;
    switch (args) {
        case margs::ALU_ADD:  cs.isAdd = true;  break;
        case margs::ALU_SUB:  cs.isSub = true;  break;
        case margs::ALU_MUL:  cs.isMul = true;  break;
        case margs::ALU_DIV:  cs.isDiv = true;  break;
        case margs::ALU_MOD:  cs.isMod = true;  break;
        case margs::ALU_CMP:  cs.isCmp = true;  break;
        case margs::ALU_AND:  cs.isAnd = true;  break;
        case margs::ALU_OR:   cs.isOR  = true;  break;
        case margs::ALU_NOT:  cs.isNot = true;  break;
        case margs::ALU_XOR:  cs.isXor = true;  break;
        case margs::ALU_MOV:  cs.isMov = true;  break;
        case margs::ALU_MOVU: cs.isMovu = true; break;
        case margs::ALU_MOVH: cs.isMovh = true; break;
        case margs::ALU_LSL:  cs.isLsl = true;  break;
        case margs::ALU_LSR:  cs.isLsr = true;  break;
        case margs::ALU_ASR:  cs.isAsr = true;  break;
        default: throw std::logic_error("unknown ALU operation");
    }

    const ALUResult r = alu_.execute(cs, static_cast<int32_t>(regs_[mdIdx(MReg::A)]),
                                         static_cast<int32_t>(regs_[mdIdx(MReg::B)]));
    if (r.divByZero) {                              // no result is produced
        raise(mcause::DIV_BY_ZERO);
        return;
    }
    regs_[mdIdx(MReg::ALU_RESULT)] = r.value;

    if (r.updateFlags) {                            // cmp
        regs_[mdIdx(MReg::FLAG_Z)] = r.zero;
        regs_[mdIdx(MReg::FLAG_N)] = r.neg;
        regs_[mdIdx(MReg::FLAG_O)] = r.over;
        regs_[mdIdx(MReg::FLAG_C)] = r.carry;
        regs_[mdIdx(MReg::FLAG_GT)] = (!r.zero && (r.neg == r.over));
        regs_[mdIdx(MReg::FLAG_LT)] = (r.neg != r.over);
        m_.zero = r.zero;  m_.neg = r.neg;          // architectural flags stay in MachineState
        m_.over = r.over;  m_.carry = r.carry;
    }
}
