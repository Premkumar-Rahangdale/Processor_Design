#pragma once
#include <cstdint>
#include "controlunit.hpp"

// The last six registers hold IEEE-754 binary32 values. An add/sub/mul/div in register form (I = 0)
// whose two source registers are both float registers is executed by the floating-point unit.
constexpr int kFirstFloatReg = 26;
constexpr int kLastFloatReg  = 31;
constexpr bool isFloatReg(int r) { return r >= kFirstFloatReg && r <= kLastFloatReg; }

struct ALUResult {
    uint32_t value = 0;        // goes to aluResult
    bool zero = false;
    bool neg = false;          // raw sign bit of A - B (for cmp)
    bool over = false;         // signed overflow of the adder
    bool carry = false;        // adder carry-out (for A - B: 1 means "no borrow")
    bool updateFlags = false;  // true only for cmp -> EX copies the flags to the machine
    bool divByZero = false;    // div/mod with B == 0 (no exception handling yet)
};

// 32-bit combinational ALU. It never touches MachineState; the pipeline
// decides what to do with the result and the flags.

//   adder      : Han-Carlson parallel-prefix  (add, sub, cmp, ld/st address)
//   multiplier : radix-4 Booth + Wallace tree, low 32 bits only  (mul)
//   divider    : non-restoring                (div, mod)
//   shifter    : logarithmic barrel shifter   (lsl, lsr, asr)
//   logic unit : and, or, xor, not
//   move unit  : mov, movu, movh

// Branch conditions (evaluated in EX from the flags set by cmp):
//   beq : zero          bsm : neg != over          bgt : !zero && neg == over

class ALU {
public:
    ALUResult execute(const controlsignals& s, int32_t a, int32_t b) const;

private:
    struct AddOut {
        uint64_t sum;
        bool carryOut;
        bool carryIntoMsb;
    };
    struct DivOut {
        uint32_t quot;
        uint32_t rem;
        bool divByZero;
        bool overflow;         // INT_MIN / -1
    };

    static AddOut   prefixAdd(uint64_t a, uint64_t b, bool cin, int width);
    static uint32_t negate32(uint32_t x);
    static uint32_t multiply(uint32_t a, uint32_t b);
    static DivOut   divide(int32_t a, int32_t b);
    static uint32_t shift(uint32_t x, unsigned amount, bool left, bool arithmetic);
    static uint32_t logic(const controlsignals& s, uint32_t a, uint32_t b);
    static uint32_t move(const controlsignals& s, uint32_t b);

    // Floating-point unit (IEEE-754 binary32). Operands and results are the raw 32-bit
    // patterns held in the float registers r26..r31. execute() uses them when s.isFloat is set.
    static uint32_t fadd(uint32_t a, uint32_t b);
    static uint32_t fsub(uint32_t a, uint32_t b);
    static uint32_t fmul(uint32_t a, uint32_t b);
    static uint32_t fdiv(uint32_t a, uint32_t b);

    // Dual-path adder behind fadd / fsub (see the comment block in alu.cpp)
    struct FpOperand {
        bool sign;
        int exp;               // effective biased exponent: the stored one, or 1 for a subnormal
        uint32_t mant;         // 24 bits; hidden bit (bit 23) set for normal numbers, clear for subnormals
    };
    static FpOperand fpUnpack(uint32_t x, bool sign);
    static uint32_t  fpAddSub(uint32_t a, uint32_t b, bool subtract);
    static uint32_t  fpFarPath(const FpOperand& a, const FpOperand& b, bool effectiveSub);
    static uint32_t  fpNearPath(const FpOperand& a, const FpOperand& b);
    static uint32_t  fpRoundPack(bool sign, int exp, uint32_t m27);
    static int       fpLeadingZeros(uint32_t x, int width);

    // Low-latency multiplier behind fmul (see the comment block in alu.cpp)
    struct CarrySave {
        uint64_t s, c;         // 48-bit product = s + c (mod 2^48), not yet carry-propagated
    };
    static CarrySave fpMantissaProduct(uint32_t a, uint32_t b, uint64_t inject);   // Booth radix-4 + 4:2 tree
    static uint64_t  koggeStoneAdd(uint64_t a, uint64_t b, int width, bool cin = false);   // carry-propagate adder

    // Reciprocal seed behind fdiv (see the comment block in alu.cpp): for a 24-bit divisor mantissa
    // my (value y = my / 2^23, in [1,2)) returns K ~ 1/y as an integer with 22 fractional bits.
    static uint32_t  fpRecipSeed(uint32_t my);
};