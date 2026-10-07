#pragma once
#include <cstdint>
#include "controlunit.hpp"

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
};