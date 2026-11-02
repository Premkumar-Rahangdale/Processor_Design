#include "../include/microinstr.hpp"
#include <sstream>
#include <stdexcept>

// Helpers are prefixed "mi" so this file can also be #included into a single
// translation unit together with the other .cpp files without name clashes.
namespace {

// Field positions (bit 0 = LSB of the word)
//   horizontal : [ control signals 38 | imm 12 | target 10 | args 10 ]
//   vertical   : [ type 3 | src 5 | dst 5 | imm 12 | target 10 | args 10 ]
constexpr int kHArgs = 0, kHTarget = kHArgs + kArgsBits, kHImm = kHTarget + kTargetBits, kHCtrl = kHImm + kImmBits;
constexpr int kVArgs = 0, kVTarget = kVArgs + kArgsBits, kVImm = kVTarget + kTargetBits,
              kVDst = kVImm + kImmBits, kVSrc = kVDst + kRegIdBits, kVType = kVSrc + kRegIdBits;

template <size_t N>
void miPut(std::bitset<N>& w, int lo, int width, uint64_t v) {
    for (int i = 0; i < width; i++) w[lo + i] = (v >> i) & 1u;
}
template <size_t N>
uint64_t miGet(const std::bitset<N>& w, int lo, int width) {
    uint64_t v = 0;
    for (int i = 0; i < width; i++) if (w[lo + i]) v |= (1ull << i);
    return v;
}

int miSext(uint32_t v, int bits) {
    const uint32_t m = 1u << (bits - 1);
    return static_cast<int>((v ^ m) - m);
}

void miCheckImm(int imm) {
    if (imm < -(1 << (kImmBits - 1)) || imm >= (1 << (kImmBits - 1)))
        throw std::out_of_range("micro-immediate does not fit in 12 bits: " + std::to_string(imm));
}
void miCheckTarget(unsigned t) {
    if (t >= static_cast<unsigned>(kControlMemWords))
        throw std::out_of_range("micro-address does not fit in 10 bits: " + std::to_string(t));
}
void miCheckArgs(unsigned a) {
    if (a >= (1u << kArgsBits)) throw std::out_of_range("args do not fit in 10 bits");
}

const char* const kMRegNames[kNumMRegs] = {
    "pc", "ir", "I", "rd", "rs1", "rs2", "immx", "branchTarget",
    "regSrc", "regData", "regVal", "A", "B",
    "flags.Z", "flags.N", "flags.O", "flags.C", "flags.GT", "flags.LT",
    "aluResult", "mar", "mdr", "ldResult", "excCause"
};

const char* const kMOpNames[8] = { "mloadIR", "mdecode", "mswitch", "mmov", "mmovi", "madd", "mbeq", "mb" };

}  // namespace

// ---------------------------------------------------------------------------
//  Which control signals does a microinstruction raise?
// ---------------------------------------------------------------------------
MicroSignals signalsFor(MicroOp op, MReg dst, MReg src, int imm, uint16_t target, uint16_t args) {
    MicroSignals s;
    s.imm = imm;
    s.target = target;
    s.args = args;

    auto out = [&s](MReg r) {
        if (!mregCanOut(r)) throw std::invalid_argument(std::string(mregName(r)) + " cannot drive the write bus");
        s.out |= 1u << static_cast<int>(r);
    };
    auto in = [&s](MReg r) {
        if (!mregCanIn(r)) throw std::invalid_argument(std::string(mregName(r)) + " cannot load from the read bus");
        s.in |= 1u << static_cast<int>(r);
    };

    switch (op) {
        case MicroOp::MLOADIR: in(MReg::IR); break;
        case MicroOp::MDECODE: break;                                       // wait one cycle
        case MicroOp::MSWITCH: s.fetch = kFetchSwitch; break;
        case MicroOp::MMOV:    out(src); in(dst); s.xfer = kXferBus; break;
        case MicroOp::MMOVI:   in(dst); s.xfer = kXferImm; break;
        case MicroOp::MADD:    out(dst); in(dst); s.xfer = kXferAdder; break;
        case MicroOp::MBEQ:    out(src); s.fetch = kFetchCond; break;
        case MicroOp::MB:      s.fetch = kFetchBranch; break;
    }
    return s;
}

// ---------------------------------------------------------------------------
//  Horizontal word: the control signals are stored directly
// ---------------------------------------------------------------------------
HorizontalWord encodeHorizontal(const MicroSignals& s) {
    miCheckImm(s.imm);
    miCheckTarget(s.target);
    miCheckArgs(s.args);

    uint64_t ctrl = 0;
    for (int r = 0; r < kNumMRegs; r++) {
        const MReg reg = static_cast<MReg>(r);
        if (mregCanOut(reg) && ((s.out >> r) & 1u)) ctrl |= 1ull << outSignalIndex(reg);
        if (mregCanIn(reg)  && ((s.in  >> r) & 1u)) ctrl |= 1ull << inSignalIndex(reg);
    }
    ctrl |= static_cast<uint64_t>(s.xfer)  << (kNumOutSignals + kNumInSignals);
    ctrl |= static_cast<uint64_t>(s.fetch) << (kNumOutSignals + kNumInSignals + kXferBits);

    HorizontalWord w;
    miPut(w, kHCtrl, kControlSignalBits, ctrl);
    miPut(w, kHImm, kImmBits, static_cast<uint32_t>(s.imm) & ((1u << kImmBits) - 1));
    miPut(w, kHTarget, kTargetBits, s.target);
    miPut(w, kHArgs, kArgsBits, s.args);
    return w;
}

MicroSignals unpackHorizontal(const HorizontalWord& w) {
    MicroSignals s;
    const uint64_t ctrl = miGet(w, kHCtrl, kControlSignalBits);
    for (int r = 0; r < kNumMRegs; r++) {
        const MReg reg = static_cast<MReg>(r);
        if (mregCanOut(reg) && ((ctrl >> outSignalIndex(reg)) & 1u)) s.out |= 1u << r;
        if (mregCanIn(reg)  && ((ctrl >> inSignalIndex(reg))  & 1u)) s.in  |= 1u << r;
    }
    s.xfer  = static_cast<uint8_t>((ctrl >> (kNumOutSignals + kNumInSignals)) & 3u);
    s.fetch = static_cast<uint8_t>((ctrl >> (kNumOutSignals + kNumInSignals + kXferBits)) & 3u);
    s.imm = miSext(static_cast<uint32_t>(miGet(w, kHImm, kImmBits)), kImmBits);
    s.target = static_cast<uint16_t>(miGet(w, kHTarget, kTargetBits));
    s.args = static_cast<uint16_t>(miGet(w, kHArgs, kArgsBits));
    return s;
}

// ---------------------------------------------------------------------------
//  Vertical word: type + register ids + fields; signals are generated by the
//  micro-decode unit
// ---------------------------------------------------------------------------
VerticalWord encodeVertical(const MicroInstr& m, uint16_t targetAddr) {
    miCheckImm(m.imm);
    miCheckTarget(targetAddr);
    miCheckArgs(m.args);

    const bool usesSrc = (m.op == MicroOp::MMOV || m.op == MicroOp::MBEQ);
    const bool usesDst = (m.op == MicroOp::MMOV || m.op == MicroOp::MMOVI || m.op == MicroOp::MADD);
    const bool usesImm = (m.op == MicroOp::MMOVI || m.op == MicroOp::MADD || m.op == MicroOp::MBEQ);
    const bool usesTarget = (m.op == MicroOp::MBEQ || m.op == MicroOp::MB);

    VerticalWord w;
    miPut(w, kVType, kTypeBits, static_cast<uint64_t>(m.op));
    miPut(w, kVSrc, kRegIdBits, usesSrc ? static_cast<uint64_t>(m.src) : 0);
    miPut(w, kVDst, kRegIdBits, usesDst ? static_cast<uint64_t>(m.dst) : 0);
    miPut(w, kVImm, kImmBits, usesImm ? (static_cast<uint32_t>(m.imm) & ((1u << kImmBits) - 1)) : 0);
    miPut(w, kVTarget, kTargetBits, usesTarget ? targetAddr : 0);
    miPut(w, kVArgs, kArgsBits, m.args);
    return w;
}

MicroSignals decodeVertical(const VerticalWord& w) {
    const MicroOp op = static_cast<MicroOp>(miGet(w, kVType, kTypeBits));
    const unsigned src = static_cast<unsigned>(miGet(w, kVSrc, kRegIdBits));
    const unsigned dst = static_cast<unsigned>(miGet(w, kVDst, kRegIdBits));
    if (src >= static_cast<unsigned>(kNumMRegs) || dst >= static_cast<unsigned>(kNumMRegs))
        throw std::invalid_argument("vertical word names a micro-register that does not exist");
    const int imm = miSext(static_cast<uint32_t>(miGet(w, kVImm, kImmBits)), kImmBits);
    const uint16_t target = static_cast<uint16_t>(miGet(w, kVTarget, kTargetBits));
    const uint16_t args = static_cast<uint16_t>(miGet(w, kVArgs, kArgsBits));
    return signalsFor(op, static_cast<MReg>(dst), static_cast<MReg>(src), imm, target, args);
}

// ---------------------------------------------------------------------------
//  Text
// ---------------------------------------------------------------------------
const char* mregName(MReg r) {
    const int i = static_cast<int>(r);
    return (i >= 0 && i < kNumMRegs) ? kMRegNames[i] : "?";
}

std::string argsName(uint16_t a) {
    switch (a) {
        case 0: return "";
        case margs::ALU_ADD:  return "<add>";
        case margs::ALU_SUB:  return "<sub>";
        case margs::ALU_MUL:  return "<mul>";
        case margs::ALU_DIV:  return "<div>";
        case margs::ALU_MOD:  return "<mod>";
        case margs::ALU_CMP:  return "<cmp>";
        case margs::ALU_AND:  return "<and>";
        case margs::ALU_OR:   return "<or>";
        case margs::ALU_NOT:  return "<not>";
        case margs::ALU_XOR:  return "<xor>";
        case margs::ALU_MOV:  return "<mov>";
        case margs::ALU_MOVU: return "<movu>";
        case margs::ALU_MOVH: return "<movh>";
        case margs::ALU_LSL:  return "<lsl>";
        case margs::ALU_LSR:  return "<lsr>";
        case margs::ALU_ASR:  return "<asr>";
        case margs::READ:     return "<read>";
        case margs::WRITE:    return "<write>";
        case margs::LOAD:     return "<load>";
        case margs::STORE:    return "<store>";
        case margs::HALT:     return "<halt>";
        default: {
            std::ostringstream o;
            o << "<0x" << std::hex << a << ">";
            return o.str();
        }
    }
}

std::string toText(const MicroInstr& m) {
    std::ostringstream o;
    o << kMOpNames[static_cast<int>(m.op)];
    const std::string a = argsName(m.args);
    switch (m.op) {
        case MicroOp::MLOADIR:
        case MicroOp::MDECODE:
        case MicroOp::MSWITCH:
            break;
        case MicroOp::MMOV:
            o << " " << mregName(m.dst) << ", " << mregName(m.src);
            if (!a.empty()) o << ", " << a;
            break;
        case MicroOp::MMOVI:
        case MicroOp::MADD:
            o << " " << mregName(m.dst) << ", " << m.imm;
            if (!a.empty()) o << ", " << a;
            break;
        case MicroOp::MBEQ:
            o << " " << mregName(m.src) << ", " << m.imm << ", " << m.target;
            break;
        case MicroOp::MB:
            o << " " << m.target;
            break;
    }
    return o.str();
}
