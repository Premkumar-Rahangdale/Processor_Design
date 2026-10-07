#include "../include/alu.hpp"
#include <vector>

// ======================================================================
// Adder: Han-Carlson parallel-prefix, any width up to 64 bits.
//
// Every bit gets a (generate, propagate) pair. Two pairs are merged with
//     (G,P) o (G',P') = (G | P&G',  P&P')        (left pair = more significant)
// and after the tree, G[i] is the carry out of bit i.
//
//   level 0      : each odd bit merges with the even bit just below it
//   levels 1..k  : Kogge-Stone on the odd bits only (distance 2, 4, 8, ...)
//   last level   : each even bit merges with the odd prefix just below it
//
// For 32 bits that is 1 + 4 + 1 = 6 levels. The carry-in is folded into bit 0.
// ======================================================================
ALU::AddOut ALU::prefixAdd(uint64_t a, uint64_t b, bool cin, int width) {
    bool g0[64] = {}, p0[64] = {};     // per-bit generate / propagate (kept for the sum)
    bool G[64] = {}, P[64] = {};       // working prefix values

    for (int i = 0; i < width; i++) {
        const bool ai = (a >> i) & 1, bi = (b >> i) & 1;
        g0[i] = ai & bi;
        p0[i] = ai ^ bi;
        G[i] = g0[i];
        P[i] = p0[i];
    }
    G[0] = g0[0] | (p0[0] & cin);        // fold carry-in into bit 0

    auto merge = [](bool& Gi, bool& Pi, bool Gj, bool Pj) {
        Gi = Gi | (Pi & Gj);
        Pi = Pi & Pj;
    };

    // level 0: odd bits absorb the even bit below
    for (int i = 1; i < width; i += 2)
        merge(G[i], P[i], G[i - 1], P[i - 1]);

    // Kogge-Stone on the odd bits (double-buffered: each level reads old values)
    for (int d = 2; d < width; d <<= 1) {
        bool nG[64], nP[64];
        for (int i = 0; i < width; i++) { nG[i] = G[i]; nP[i] = P[i]; }
        for (int i = 1; i < width; i += 2)
            if (i - d >= 0) merge(nG[i], nP[i], G[i - d], P[i - d]);
        for (int i = 0; i < width; i++) { G[i] = nG[i]; P[i] = nP[i]; }
    }

    // last level: even bits take the finished odd prefix below them
    for (int i = 2; i < width; i += 2)
        merge(G[i], P[i], G[i - 1], P[i - 1]);

    // sum bit i = p[i] xor carry-into-bit-i
    uint64_t sum = 0;
    for (int i = 0; i < width; i++) {
        const bool c = (i == 0) ? cin : G[i - 1];
        if (p0[i] ^ c) sum |= (1ull << i);
    }

    AddOut o;
    o.sum = sum;
    o.carryOut = G[width - 1];
    o.carryIntoMsb = (width > 1) ? G[width - 2] : cin;
    return o;
}

// two's complement negate through the adder: ~x + 1
uint32_t ALU::negate32(uint32_t x) {
    return static_cast<uint32_t>(prefixAdd(~x, 0, true, 32).sum);
}

// ======================================================================
// Multiplier: radix-4 (modified) Booth + Wallace tree, low 32 bits only.
//
// B is cut into 16 overlapping bit triples (b[2i+1], b[2i], b[2i-1]), each
// giving a digit in {-2,-1,0,+1,+2}. Row i is digit*A shifted by 2i, kept to
// 32 bits. A negative row is stored inverted, with a "+1" bit added at column
// 2i (collected into one extra row). The 17 rows are squeezed to 2 with
// 3:2 compressors (full adders) and the last add uses the prefix adder.
// ======================================================================
uint32_t ALU::multiply(uint32_t a, uint32_t b) {
    std::vector<uint32_t> rows;
    uint32_t negBits = 0;

    for (int i = 0; i < 16; i++) {
        const bool b2 = (b >> (2 * i + 1)) & 1;
        const bool b1 = (b >> (2 * i)) & 1;
        const bool b0 = (i == 0) ? false : ((b >> (2 * i - 1)) & 1);

        const bool one = b1 ^ b0;                                   // |digit| == 1
        const bool two = (b2 & !b1 & !b0) | (!b2 & b1 & b0);       // |digit| == 2
        const bool neg = b2 & !(b1 & b0);                           // digit < 0

        uint32_t x = one ? a : (two ? (a << 1) : 0u);
        if (neg) {
            x = ~x;
            negBits |= (1u << (2 * i));                             // the +1 for this row
        }
        rows.push_back(x << (2 * i));
    }
    rows.push_back(negBits);

    // Wallace reduction: 17 -> 12 -> 8 -> 6 -> 4 -> 3 -> 2 rows
    while (rows.size() > 2) {
        std::vector<uint32_t> next;
        size_t k = 0;
        for (; k + 2 < rows.size(); k += 3) {
            const uint32_t x = rows[k], y = rows[k + 1], z = rows[k + 2];
            next.push_back(x ^ y ^ z);                              // sum bits
            next.push_back(((x & y) | (x & z) | (y & z)) << 1);     // carry bits
        }
        for (; k < rows.size(); k++) next.push_back(rows[k]);       // leftovers pass through
        rows = next;
    }

    return static_cast<uint32_t>(prefixAdd(rows[0], rows[1], false, 32).sum);
}

// ======================================================================
// Divider: non-restoring, on magnitudes, sign fixed at the end.
//
// R is a 33-bit two's complement partial remainder (33 bits so that 2R+bit
// never overflows, even for |INT_MIN|). Per step: look at the sign of R, shift
// (R:Q) left, then add D if R was negative, otherwise subtract D. The new
// quotient bit is 1 when the new R is non-negative. A last correction adds D
// back if R ended negative. Quotient truncates toward zero; the remainder takes
// the sign of the dividend. Every add/subtract goes through the prefix adder.
// ======================================================================
ALU::DivOut ALU::divide(int32_t sa, int32_t sb) {
    DivOut d{0, 0, false, false};
    if (sb == 0) { d.divByZero = true; return d; }

    const bool negA = sa < 0, negB = sb < 0;
    const uint32_t N = negA ? negate32(static_cast<uint32_t>(sa)) : static_cast<uint32_t>(sa);
    const uint32_t D = negB ? negate32(static_cast<uint32_t>(sb)) : static_cast<uint32_t>(sb);

    const int W = 33;
    const uint64_t mask = (1ull << W) - 1;
    const uint64_t signBit = 1ull << (W - 1);

    uint64_t R = 0;
    uint32_t Q = N;
    for (int i = 0; i < 32; i++) {
        const bool wasNeg = (R & signBit) != 0;
        R = ((R << 1) | (Q >> 31)) & mask;                          // shift R:Q left
        Q <<= 1;
        R = wasNeg ? prefixAdd(R, D, false, W).sum                   // R + D
                   : prefixAdd(R, (~static_cast<uint64_t>(D)) & mask, true, W).sum;  // R - D
        if (!(R & signBit)) Q |= 1u;
    }
    if (R & signBit) R = prefixAdd(R, D, false, W).sum;              // final correction

    uint32_t rem = static_cast<uint32_t>(R);
    d.quot = (negA != negB) ? negate32(Q) : Q;
    d.rem  = negA ? negate32(rem) : rem;
    d.overflow = (sa == INT32_MIN && sb == -1);                      // quotient wraps to INT_MIN
    return d;
}

// ======================================================================
// Shifter: logarithmic barrel shifter, 5 mux layers (1, 2, 4, 8, 16).
// Layer k is bypassed unless bit k of the amount is set. asr fills with the
// original sign bit, lsl/lsr with zeros.
// ======================================================================
uint32_t ALU::shift(uint32_t x, unsigned amount, bool left, bool arithmetic) {
    const uint32_t fill = (arithmetic && (x >> 31)) ? 0xFFFFFFFFu : 0u;
    for (int k = 0; k < 5; k++) {
        if (!((amount >> k) & 1)) continue;
        const unsigned s = 1u << k;
        if (left) x = x << s;
        else      x = (x >> s) | (fill & ~(0xFFFFFFFFu >> s));
    }
    return x;
}

// ---------------------------------------------------------------- logic / move
uint32_t ALU::logic(const controlsignals& s, uint32_t a, uint32_t b) {
    if (s.isAnd) return a & b;
    if (s.isOR)  return a | b;
    if (s.isXor) return a ^ b;
    return ~b;                                   // isNot: source is B (assembler puts it there)
}

uint32_t ALU::move(const controlsignals& s, uint32_t b) {
    if (s.isMovu) return b & 0xFFFFu;            // lower 16 bits, upper cleared
    if (s.isMovh) return (b & 0xFFFFu) << 16;    // upper 16 bits, lower cleared
    return b;                                    // mov
}


ALUResult ALU::execute(const controlsignals& s, int32_t sa, int32_t sb) const {
    const uint32_t a = static_cast<uint32_t>(sa);
    const uint32_t b = static_cast<uint32_t>(sb);
    ALUResult r;

    auto fromAdder = [&r](const AddOut& o) {
        r.value = static_cast<uint32_t>(o.sum);
        r.zero  = (r.value == 0);
        r.neg   = ((r.value >> 31) & 1) != 0;
        r.carry = o.carryOut;
        r.over  = o.carryOut ^ o.carryIntoMsb;   // signed overflow
    };

    if (s.isAdd || s.isLd || s.isSt) {           // ld/st: effective address = base + offset
        fromAdder(prefixAdd(a, b, false, 32));
    } else if (s.isSub || s.isCmp) {             // A - B = A + ~B + 1
        fromAdder(prefixAdd(a, ~b, true, 32));
        r.updateFlags = s.isCmp;                 // only cmp changes the machine flags
    } else if (s.isMul) {
        r.value = multiply(a, b);
    } else if (s.isDiv || s.isMod) {
        const DivOut d = divide(sa, sb);
        r.divByZero = d.divByZero;
        r.over = d.overflow;
        r.value = s.isDiv ? d.quot : d.rem;
    } else if (s.isLsl) {
        r.value = shift(a, b & 31u, true, false);
    } else if (s.isLsr) {
        r.value = shift(a, b & 31u, false, false);
    } else if (s.isAsr) {
        r.value = shift(a, b & 31u, false, true);
    } else if (s.isAnd || s.isOR || s.isXor || s.isNot) {
        r.value = logic(s, a, b);
    } else if (s.isMov || s.isMovu || s.isMovh) {
        r.value = move(s, b);
    }
    return r;
}