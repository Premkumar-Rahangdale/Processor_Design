#pragma once
#include <bitset>
#include <cstdint>
#include <string>

// ===========================================================================
//  RISC201 microprogrammed control: shared definitions
//
//  Follows the book / lectures (Chapter 8): 8 microinstructions, a shared bus
//  built from a write bus, a transfer multiplexer and a read bus, and two
//  microinstruction encodings (vertical and horizontal).
//
//  Adapted to RISC201: 32 architectural registers (5-bit ids), flags
//  Z/N/O/C plus the derived GT/LT, and an exception-cause register.
// ===========================================================================

// ---------------------------------------------------------------------------
//  Micro-registers (the only registers visible to microcode)
// ---------------------------------------------------------------------------
enum class MReg : uint8_t {
    PC = 0,
    IR,
    I,              // immediate bit            (decode unit)
    RD,             // destination register id  (decode unit)
    RS1,            // source register 1 id     (decode unit)
    RS2,            // source register 2 id     (decode unit)
    IMMX,           // sign-extended 16-bit imm (decode unit)
    BRANCH_TARGET,  // pc + 27-bit offset       (decode unit)
    REG_SRC,        // register-file address
    REG_DATA,       // register-file write data
    REG_VAL,        // register-file read data
    A,              // ALU operand 1
    B,              // ALU operand 2
    FLAG_Z,         // flags set by cmp
    FLAG_N,
    FLAG_O,
    FLAG_C,
    FLAG_GT,        // derived on cmp: !Z && N == O   (signed A > B)
    FLAG_LT,        // derived on cmp: N != O          (signed A < B)
    ALU_RESULT,
    MAR,
    MDR,
    LD_RESULT,
    EXC_CAUSE,      // set by hardware when a unit raises an exception
    COUNT
};
constexpr int kNumMRegs = static_cast<int>(MReg::COUNT);   // 24

// Bus connectivity (book, section 8.7): every register except the decode
// registers, the flags and ir is connected to both buses.
constexpr bool mregCanOut(MReg r) { return r != MReg::IR; }   // can drive the write bus
constexpr bool mregCanIn(MReg r) {                            // can load from the read bus
    switch (r) {
        case MReg::PC:
        case MReg::IR:
        case MReg::REG_SRC:
        case MReg::REG_DATA:
        case MReg::REG_VAL:
        case MReg::A:
        case MReg::B:
        case MReg::ALU_RESULT:
        case MReg::MAR:
        case MReg::MDR:
        case MReg::LD_RESULT:
            return true;
        default:
            return false;
    }
}

constexpr int mregCountOut() {
    int n = 0;
    for (int i = 0; i < kNumMRegs; i++) if (mregCanOut(static_cast<MReg>(i))) n++;
    return n;
}
constexpr int mregCountIn() {
    int n = 0;
    for (int i = 0; i < kNumMRegs; i++) if (mregCanIn(static_cast<MReg>(i))) n++;
    return n;
}

// ---------------------------------------------------------------------------
//  Word formats
// ---------------------------------------------------------------------------
constexpr int kNumOutSignals = mregCountOut();   // 23: one "drive write bus" signal per register
constexpr int kNumInSignals  = mregCountIn();    // 11: one "load from read bus" signal per register
constexpr int kXferBits  = 2;                    // transfer multiplexer select
constexpr int kFetchBits = 2;                    // micro-fetch multiplexer select
constexpr int kControlSignalBits = kNumOutSignals + kNumInSignals + kXferBits + kFetchBits;   // 38

constexpr int kImmBits    = 12;                  // micro-immediate (-2048 .. 2047)
constexpr int kTargetBits = 10;                  // micro-PC / branch target (1024 words)
constexpr int kArgsBits   = 10;                  // 3-bit unit id + 7-bit unit opcode
constexpr int kTypeBits   = 3;                   // 8 microinstructions
constexpr int kRegIdBits  = 5;                   // micro-register id

constexpr int kHorizontalBits = kControlSignalBits + kImmBits + kTargetBits + kArgsBits;       // 70
constexpr int kVerticalBits   = kTypeBits + 2 * kRegIdBits + kImmBits + kTargetBits + kArgsBits;   // 45
constexpr int kControlMemWords = 1 << kTargetBits;                                             // 1024

static_assert(kNumMRegs <= (1 << kRegIdBits), "micro-register id does not fit the vertical src/dest fields");
static_assert(kControlSignalBits == 38 && kHorizontalBits == 70 && kVerticalBits == 45,
              "word widths changed: update docs/Microcode.MD");

using HorizontalWord = std::bitset<kHorizontalBits>;
using VerticalWord   = std::bitset<kVerticalBits>;

constexpr int outSignalIndex(MReg r) {
    int n = 0;
    for (int i = 0; i < static_cast<int>(r); i++) if (mregCanOut(static_cast<MReg>(i))) n++;
    return n;
}
constexpr int inSignalIndex(MReg r) {
    int n = 0;
    for (int i = 0; i < static_cast<int>(r); i++) if (mregCanIn(static_cast<MReg>(i))) n++;
    return kNumOutSignals + n;
}

// ---------------------------------------------------------------------------
//  The 8 microinstructions (book Table 8.8). The numeric value is the 3-bit
//  type field of the vertical encoding.
// ---------------------------------------------------------------------------
enum class MicroOp : uint8_t {
    MLOADIR = 0,   // ir <- [pc]
    MDECODE = 1,   // wait one cycle: decode registers get populated
    MSWITCH = 2,   // micro-pc <- start of the routine for the opcode in ir
    MMOV    = 3,   // dst <- src [, args]
    MMOVI   = 4,   // dst <- imm [, args]
    MADD    = 5,   // dst <- dst + imm [, args]
    MBEQ    = 6,   // if (src == imm) micro-pc <- target
    MB      = 7    // micro-pc <- target
};

// Micro-fetch multiplexer select (book Table 8.9)
constexpr uint8_t kFetchNext   = 0;   // micro-pc + 1
constexpr uint8_t kFetchBranch = 1;   // branch target
constexpr uint8_t kFetchSwitch = 2;   // output of the switch unit
constexpr uint8_t kFetchCond   = 3;   // branch target if isMBranch, else micro-pc + 1

// Transfer multiplexer select (book section 8.7)
constexpr uint8_t kXferBus   = 0;     // write bus
constexpr uint8_t kXferAdder = 1;     // micro-adder: write bus + micro-immediate
constexpr uint8_t kXferImm   = 2;     // micro-immediate

// ---------------------------------------------------------------------------
//  <args>: 10 bits = 3-bit unit id | 7-bit unit opcode. All-zero = no operation.
// ---------------------------------------------------------------------------
namespace margs {
    constexpr uint16_t UNIT_ALU = 0, UNIT_REGFILE = 1, UNIT_MEM = 2, UNIT_EXC = 3;
    constexpr uint16_t mk(uint16_t unit, uint16_t op) { return static_cast<uint16_t>((unit << 7) | op); }

    // ALU: the opcode equals the instruction's ISA opcode
    constexpr uint16_t ALU_ADD = mk(UNIT_ALU, 1),  ALU_SUB  = mk(UNIT_ALU, 2),  ALU_MUL  = mk(UNIT_ALU, 3),
                       ALU_DIV = mk(UNIT_ALU, 4),  ALU_MOD  = mk(UNIT_ALU, 5),  ALU_CMP  = mk(UNIT_ALU, 6),
                       ALU_AND = mk(UNIT_ALU, 7),  ALU_OR   = mk(UNIT_ALU, 8),  ALU_NOT  = mk(UNIT_ALU, 9),
                       ALU_XOR = mk(UNIT_ALU, 10), ALU_MOV  = mk(UNIT_ALU, 11), ALU_MOVU = mk(UNIT_ALU, 12),
                       ALU_MOVH = mk(UNIT_ALU, 13), ALU_LSL = mk(UNIT_ALU, 14), ALU_LSR  = mk(UNIT_ALU, 15),
                       ALU_ASR = mk(UNIT_ALU, 16);
    // Register file
    constexpr uint16_t READ  = mk(UNIT_REGFILE, 1), WRITE = mk(UNIT_REGFILE, 2);
    // Data memory
    constexpr uint16_t LOAD  = mk(UNIT_MEM, 1),     STORE = mk(UNIT_MEM, 2);
    // Exception unit
    constexpr uint16_t HALT  = mk(UNIT_EXC, 1);
}

// Exception cause codes (saved in R4 by the exception microroutine)
namespace mcause {
    constexpr uint32_t NONE = 0;
    constexpr uint32_t ILLEGAL_OPCODE = 1;
    constexpr uint32_t DIV_BY_ZERO    = 2;
    constexpr uint32_t MEM_ADDRESS    = 3;   // data address outside data memory
    constexpr uint32_t STACK_BOUNDS   = 4;   // reserved for the stack-protection phase
    constexpr uint32_t ARITH_OVERFLOW = 5;   // reserved
}

// ---------------------------------------------------------------------------
//  Symbolic microinstruction (one row of a microprogram table)
// ---------------------------------------------------------------------------
struct MicroInstr {
    std::string label;          // optional label of this control-memory word
    MicroOp op = MicroOp::MDECODE;
    MReg dst = MReg::PC;
    MReg src = MReg::PC;
    int imm = 0;
    std::string target;         // label (mb / mbeq)
    uint16_t args = 0;

    MicroInstr& at(std::string l) { label = std::move(l); return *this; }
};

// Readable builders: mi::mmov(MReg::REG_SRC, MReg::RS1, margs::READ).at(".add")
namespace mi {
    inline MicroInstr mloadIR()  { MicroInstr m; m.op = MicroOp::MLOADIR; return m; }
    inline MicroInstr mdecode()  { MicroInstr m; m.op = MicroOp::MDECODE; return m; }
    inline MicroInstr mswitch()  { MicroInstr m; m.op = MicroOp::MSWITCH; return m; }
    inline MicroInstr mmov(MReg d, MReg s, uint16_t args = 0) {
        MicroInstr m; m.op = MicroOp::MMOV; m.dst = d; m.src = s; m.args = args; return m;
    }
    inline MicroInstr mmovi(MReg d, int imm, uint16_t args = 0) {
        MicroInstr m; m.op = MicroOp::MMOVI; m.dst = d; m.imm = imm; m.args = args; return m;
    }
    inline MicroInstr madd(MReg d, int imm, uint16_t args = 0) {
        MicroInstr m; m.op = MicroOp::MADD; m.dst = d; m.imm = imm; m.args = args; return m;
    }
    inline MicroInstr mbeq(MReg s, int imm, std::string target) {
        MicroInstr m; m.op = MicroOp::MBEQ; m.src = s; m.imm = imm; m.target = std::move(target); return m;
    }
    inline MicroInstr mb(std::string target) {
        MicroInstr m; m.op = MicroOp::MB; m.target = std::move(target); return m;
    }
}

// ---------------------------------------------------------------------------
//  Control signals: what the data path actually receives each cycle.
//  Horizontal: read straight out of the word.  Vertical: produced by the
//  micro-decode unit (decodeVertical).
// ---------------------------------------------------------------------------
struct MicroSignals {
    uint32_t out = 0;        // bit r: register r drives the write bus
    uint32_t in = 0;         // bit r: register r loads from the read bus
    uint8_t  xfer = kXferBus;
    uint8_t  fetch = kFetchNext;
    int      imm = 0;        // sign-extended micro-immediate
    uint16_t target = 0;
    uint16_t args = 0;
};

// Signals implied by a microinstruction. Throws std::invalid_argument if a
// register is not connected to the bus the instruction needs.
MicroSignals signalsFor(MicroOp op, MReg dst, MReg src, int imm, uint16_t target, uint16_t args);

HorizontalWord encodeHorizontal(const MicroSignals& s);
MicroSignals   unpackHorizontal(const HorizontalWord& w);            // pure wiring
VerticalWord   encodeVertical(const MicroInstr& m, uint16_t targetAddr);
MicroSignals   decodeVertical(const VerticalWord& w);                // the micro-decode unit

// Text helpers (listings)
const char* mregName(MReg r);
std::string argsName(uint16_t args);
std::string toText(const MicroInstr& m);
