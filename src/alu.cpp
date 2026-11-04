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

// ======================================================================
// Floating-point unit: IEEE-754 binary32 on raw bit patterns, round to
// nearest even, subnormals supported.
//
// fadd / fsub: dual-path adder.  fsub(a, b) is fadd(a, -b).
//
// A single-path adder does a big right shift (align) and a big left shift
// (normalise) one after the other. The two never both happen, so the adder
// has two datapaths and a selector:
//
//   special values (NaN, Inf, zero) are resolved first.
//   swap so that operand x has the larger exponent; d = exp(x) - exp(y).
//
//   far path  : effective addition, or effective subtraction with d >= 2
//               align y right by d (barrel shifter, shifted-out bits are
//               jammed into a sticky bit), add / subtract, normalise by at
//               most one bit.
//   near path : effective subtraction with d <= 1
//               align by at most one bit, subtract, negate if the result is
//               negative (d == 0 only), count leading zeros, normalise with
//               a big left shift. The only bit that can be lost is the one
//               bit of alignment, so there is a guard bit but no sticky bit.
//
//   both paths hand a 27-bit word (24 mantissa bits + guard, round, sticky)
//   and an exponent to one rounding / packing stage.
//
// Working word m27: bit 26 is the hidden-bit position, bits 26..3 hold the
// 24-bit mantissa, bit 2 is the guard bit, bit 1 the round bit, bit 0 the
// sticky bit.
// Results: NaN operands propagate (first NaN operand, quieted); Inf - Inf
// gives the default NaN 0x7FC00000; x - x gives +0.
// ======================================================================

ALU::FpOperand ALU::fpUnpack(uint32_t x, bool sign) {
    FpOperand o;
    const uint32_t e = (x >> 23) & 0xFFu, f = x & 0x7FFFFFu;
    o.sign = sign;
    o.exp  = (e == 0) ? 1 : static_cast<int>(e);      // a subnormal has the scale of exponent 1 ...
    o.mant = (e == 0) ? f : (f | 0x800000u);          // ... and no hidden bit
    return o;
}

// priority encoder: number of leading zeros of x seen as a `width`-bit number (x != 0)
int ALU::fpLeadingZeros(uint32_t x, int width) {
    int n = 0;
    for (int i = width - 1; i >= 0 && !((x >> i) & 1u); i--) n++;
    return n;
}

// Rounding / packing stage shared by both paths.
//   m27 : hidden bit at bit 26 (clear for a subnormal result, then exp == 1), guard/round/sticky below
uint32_t ALU::fpRoundPack(bool sign, int exp, uint32_t m27) {
    uint32_t mant = m27 >> 3;                          // 24 bits
    const bool guard  = ((m27 >> 2) & 1u) != 0;
    const bool sticky = (m27 & 3u) != 0;               // round | sticky
    const bool lsb    = (mant & 1u) != 0;

    if (guard && (sticky || lsb)) {                    // round to nearest, ties to even
        mant = static_cast<uint32_t>(prefixAdd(mant, 0, true, 25).sum);   // mant + 1
        if (mant >> 24) { mant >>= 1; exp++; }         // 1.11..1 rounded up to 10.00..0
    }
    const uint32_t signBit = sign ? 0x80000000u : 0u;
    if (exp >= 255) return signBit | 0x7F800000u;      // overflow: +-Inf
    const uint32_t expField = (mant >> 23) ? static_cast<uint32_t>(exp) : 0u;   // no hidden bit: subnormal
    return signBit | (expField << 23) | (mant & 0x7FFFFFu);
}

// Far path: effective addition (any d) or effective subtraction with d >= 2.
// a has the larger exponent. For d >= 2 it is also the larger magnitude, so the result is never negative.
uint32_t ALU::fpFarPath(const FpOperand& A, const FpOperand& B, bool effectiveSub) {
    const int d = A.exp - B.exp;
    const uint32_t a27 = A.mant << 3;
    uint32_t b27 = B.mant << 3;

    // alignment shifter: shift B right by d; everything shifted out is OR-ed into the sticky bit (bit 0)
    const unsigned dd = d > 31 ? 31u : static_cast<unsigned>(d);
    const uint32_t lost = b27 & ~(0xFFFFFFFFu << dd);
    b27 = shift(b27, dd, false, false) | (lost != 0 ? 1u : 0u);

    const uint32_t mask28 = (1u << 28) - 1;
    uint32_t m;
    int e = A.exp;
    if (!effectiveSub) {
        m = static_cast<uint32_t>(prefixAdd(a27, b27, false, 28).sum);
        if (m >> 27) { m = (m >> 1) | (m & 1u); e++; }                 // carry out: normalise right by 1
    } else {
        m = static_cast<uint32_t>(prefixAdd(a27, (~b27) & mask28, true, 28).sum);   // a27 - b27
        if (!((m >> 26) & 1u)) { m <<= 1; e--; }                       // lost the leading bit: normalise left by 1
    }
    return fpRoundPack(A.sign, e, m);
}

// Near path: effective subtraction with d <= 1 (a has the larger exponent).
uint32_t ALU::fpNearPath(const FpOperand& A, const FpOperand& B) {
    const int d = A.exp - B.exp;                       // 0 or 1
    const uint32_t a25 = A.mant << 1;                  // one extra low bit: the alignment shift loses nothing
    const uint32_t b25 = (B.mant << 1) >> d;
    const uint32_t mask26 = (1u << 26) - 1;

    uint32_t diff = static_cast<uint32_t>(prefixAdd(a25, (~b25) & mask26, true, 26).sum);   // 26-bit two's complement
    bool sign = A.sign;
    if ((diff >> 25) & 1u) {                           // negative: only possible for d == 0 with B.mant > A.mant
        diff = static_cast<uint32_t>(prefixAdd((~diff) & mask26, 0, true, 26).sum);         // negate
        sign = !sign;
    }
    if (diff == 0) return 0u;                          // exact cancellation: +0 in round-to-nearest-even

    // normalise: leading-zero count, then a left shift that stops at the subnormal boundary (exponent 1)
    const int lz = fpLeadingZeros(diff, 25);
    const int sh = (lz < A.exp - 1) ? lz : (A.exp - 1);
    diff <<= sh;
    return fpRoundPack(sign, A.exp - sh, diff << 2);   // bit 24 -> bit 26; the low bit of diff is the guard bit
}

uint32_t ALU::fpAddSub(uint32_t a, uint32_t b, bool subtract) {
    const uint32_t magA = a & 0x7FFFFFFFu, magB = b & 0x7FFFFFFFu;
    const bool sa = (a >> 31) != 0;
    const bool sb = ((b >> 31) != 0) != subtract;      // sign of the second operand after fsub's flip

    // ---- special values ----
    if (magA > 0x7F800000u) return a | 0x00400000u;    // NaN: propagate the first NaN operand, quieted
    if (magB > 0x7F800000u) return b | 0x00400000u;
    const bool aInf = (magA == 0x7F800000u), bInf = (magB == 0x7F800000u);
    if (aInf && bInf) return (sa == sb) ? a : 0x7FC00000u;           // Inf - Inf is invalid: default NaN
    if (aInf) return a;
    if (bInf) return (sb ? 0x80000000u : 0u) | magB;
    if (magA == 0 && magB == 0) return (sa && sb) ? 0x80000000u : 0u;   // -0 only for (-0) + (-0)
    if (magA == 0) return (sb ? 0x80000000u : 0u) | magB;
    if (magB == 0) return a;

    // ---- finite, non-zero operands ----
    FpOperand x = fpUnpack(a, sa), y = fpUnpack(b, sb);
    if (y.exp > x.exp) { const FpOperand t = x; x = y; y = t; }      // x has the larger exponent
    const int d = x.exp - y.exp;
    const bool effectiveSub = (x.sign != y.sign);

    if (effectiveSub && d <= 1) return fpNearPath(x, y);
    return fpFarPath(x, y, effectiveSub);
}

uint32_t ALU::fadd(uint32_t a, uint32_t b) { return fpAddSub(a, b, false); }
uint32_t ALU::fsub(uint32_t a, uint32_t b) { return fpAddSub(a, b, true); }

// ======================================================================
// Floating-point multiplier (binary32), organised for the lowest latency.
//
//   sign      : sa xor sb
//   exponent  : ea + eb - 127   (+1 when the product is >= 2)
//   mantissa  : 24 x 24 -> 48-bit product P in [2^46, 2^48), in three stages:
//
//   1. radix-4 Booth recoding of B: 13 partial products instead of 24. The
//      "+1" of a negative row goes into the empty low columns of the next row,
//      so it needs no row of its own.
//   2. 4:2 compressor tree: 13 -> 7 -> 4 -> 2 rows, three levels (the three
//      rows left over in the second level go through one 3:2 counter). A 4:2
//      compressor is two chained full adders; its horizontal carry (cout -> cin
//      of the next bit) never ripples, so a level costs about 3 XOR delays.
//   3. one Kogge-Stone adder: log2(48) -> 6 levels, one fewer than Han-Carlson.
//
// Rounding is merged into that last addition, so nothing follows it except a
// mux. P >= 2^47 keeps P[47:24] (half an ulp is 2^23); otherwise P[46:23] is
// kept (half an ulp is 2^22). Which case applies is known only after the add,
// so both candidates are computed in parallel:
//     tX = P + 2^23        tY = P + 2^22      and the plain P for the select (its MSB)
// The half-ulp bit rides in the compressor tree as one extra row (14 -> 8 -> 4 -> 2, still three
// levels). After adding half an ulp the discarded bits are all zero exactly
// when they were a tie, and then the result LSB is cleared (round to even).
// Rounding up can carry out of the mantissa only in the tY case; that bumps
// the exponent. The model evaluates the three variants separately.
//
// Results whose exponent could leave 1..254 (underflow into subnormals, or
// overflow) take a slow path: the exact P is shifted right with a sticky bit
// and rounded by fpRoundPack, like fadd. Hardware would give that path an
// extra cycle.
// Special values: NaN operands propagate (first NaN, quieted); 0 * Inf gives
// the default NaN 0x7FC00000; Inf * x gives +-Inf; 0 * x gives +-0.
// ======================================================================

// Booth radix-4 partial products + 4:2 compressor tree.
//   a, b   : 24-bit mantissas (unsigned);  inject : optional extra row (the half-ulp bit), 0 for none
//   returns: the two final rows, 48 bits wide
ALU::CarrySave ALU::fpMantissaProduct(uint32_t a, uint32_t b, uint64_t inject) {
    const uint64_t M48 = (1ull << 48) - 1;

    // 3:2 counter on whole rows: x + y + z = s + c  (mod 2^48)
    auto csa32 = [&](uint64_t x, uint64_t y, uint64_t z, uint64_t& s, uint64_t& c) {
        s = x ^ y ^ z;
        c = (((x & y) | (x & z) | (y & z)) << 1) & M48;
    };
    // 4:2 compressor on whole rows: x1 + x2 + x3 + x4 = s + c  (mod 2^48)
    //   cout of bit i feeds cin of bit i+1, and cout does not depend on cin
    auto csa42 = [&](uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t& s, uint64_t& c) {
        const uint64_t cout = (x1 & x2) | (x1 & x3) | (x2 & x3);
        const uint64_t s1   = x1 ^ x2 ^ x3;
        const uint64_t cin  = (cout << 1) & M48;
        s = s1 ^ x4 ^ cin;
        c = (((s1 & x4) | (s1 & cin) | (x4 & cin)) << 1) & M48;
    };

    // ---- 1. Booth radix-4: digit i from the triple (b[2i+1], b[2i], b[2i-1]), digit in {-2..+2} ----
    // b is 24 bits unsigned, so digit 12 sees (0, 0, b23): never negative.
    std::vector<uint64_t> rows;
    bool prevNeg = false;
    for (int i = 0; i < 13; i++) {
        const bool b2 = (b >> (2 * i + 1)) & 1;
        const bool b1 = (b >> (2 * i)) & 1;
        const bool b0 = (i == 0) ? false : ((b >> (2 * i - 1)) & 1);

        const bool one = b1 ^ b0;                                   // |digit| == 1
        const bool two = (b2 & !b1 & !b0) | (!b2 & b1 & b0);       // |digit| == 2
        const bool neg = b2 & !(b1 & b0);                           // digit < 0

        uint64_t x = one ? static_cast<uint64_t>(a) : (two ? (static_cast<uint64_t>(a) << 1) : 0ull);
        if (neg) x = ~x & M48;                                      // inverted over all 48 bits (sign extension)
        uint64_t row = (x << (2 * i)) & M48;
        if (prevNeg) row |= 1ull << (2 * (i - 1));                  // the +1 of row i-1: columns below 2i are empty here
        prevNeg = neg;
        rows.push_back(row);
    }
    if (inject) rows.push_back(inject);

    // ---- 2. 4:2 tree (leftover group of three -> one 3:2 counter, anything less passes through) ----
    while (rows.size() > 2) {
        std::vector<uint64_t> next;
        size_t k = 0;
        uint64_t s, c;
        for (; k + 3 < rows.size(); k += 4) {
            csa42(rows[k], rows[k + 1], rows[k + 2], rows[k + 3], s, c);
            next.push_back(s);
            next.push_back(c);
        }
        if (rows.size() - k == 3) {
            csa32(rows[k], rows[k + 1], rows[k + 2], s, c);
            next.push_back(s);
            next.push_back(c);
            k += 3;
        }
        for (; k < rows.size(); k++) next.push_back(rows[k]);
        rows = next;
    }
    return CarrySave{rows[0], rows[1]};
}

// Kogge-Stone parallel-prefix adder: every level merges each bit with the bit
// d positions below (d = 1, 2, 4, ...), so ceil(log2(width)) levels in total.
// Returns the low `width` bits of a + b + cin (cin folded into bit 0, as in prefixAdd).
uint64_t ALU::koggeStoneAdd(uint64_t a, uint64_t b, int width, bool cin) {
    bool p0[64] = {}, G[64] = {}, P[64] = {};
    for (int i = 0; i < width; i++) {
        const bool ai = (a >> i) & 1, bi = (b >> i) & 1;
        G[i]  = ai & bi;
        P[i]  = ai ^ bi;
        p0[i] = P[i];
    }
    G[0] = G[0] | (p0[0] & cin);                                    // fold the carry-in into bit 0
    for (int d = 1; d < width; d <<= 1) {                           // double-buffered: each level reads old values
        bool nG[64], nP[64];
        for (int i = 0; i < width; i++) { nG[i] = G[i]; nP[i] = P[i]; }
        for (int i = d; i < width; i++) {
            nG[i] = G[i] | (P[i] & G[i - d]);
            nP[i] = P[i] & P[i - d];
        }
        for (int i = 0; i < width; i++) { G[i] = nG[i]; P[i] = nP[i]; }
    }
    uint64_t sum = 0;
    for (int i = 0; i < width; i++) {
        const bool c = (i == 0) ? cin : G[i - 1];                   // carry into bit i
        if (p0[i] ^ c) sum |= (1ull << i);
    }
    return sum;
}

uint32_t ALU::fmul(uint32_t a, uint32_t b) {
    const uint32_t magA = a & 0x7FFFFFFFu, magB = b & 0x7FFFFFFFu;
    const bool sign = ((a ^ b) >> 31) != 0;
    const uint32_t signBit = sign ? 0x80000000u : 0u;

    // ---- special values ----
    if (magA > 0x7F800000u) return a | 0x00400000u;                  // NaN: propagate the first NaN operand, quieted
    if (magB > 0x7F800000u) return b | 0x00400000u;
    const bool aInf = (magA == 0x7F800000u), bInf = (magB == 0x7F800000u);
    if ((aInf && magB == 0) || (bInf && magA == 0)) return 0x7FC00000u;   // 0 * Inf is invalid: default NaN
    if (aInf || bInf) return signBit | 0x7F800000u;
    if (magA == 0 || magB == 0) return signBit;                      // signed zero

    // ---- unpack; a subnormal operand is normalised so its leading 1 sits at bit 23 ----
    const FpOperand x = fpUnpack(a, false), y = fpUnpack(b, false);
    const int lzx = fpLeadingZeros(x.mant, 24), lzy = fpLeadingZeros(y.mant, 24);
    const uint32_t mx = x.mant << lzx, my = y.mant << lzy;
    const int e0 = (x.exp - lzx) + (y.exp - lzy) - 127;              // exponent field when P < 2^47

    // ---- exact product (needed for the MSB select and for the slow path) ----
    const CarrySave plain = fpMantissaProduct(mx, my, 0);
    const uint64_t P = koggeStoneAdd(plain.s, plain.c, 48);
    const bool top = ((P >> 47) & 1u) != 0;

    // ---- slow path: the result may be subnormal or overflow ----
    if (e0 < 1 || e0 > 253) {
        int e = e0;
        uint32_t m27;                                                // hidden bit at 26, then guard / round / sticky
        if (top) { e++; m27 = static_cast<uint32_t>(P >> 21) | ((P & 0x1FFFFFull) != 0 ? 1u : 0u); }
        else     {      m27 = static_cast<uint32_t>(P >> 20) | ((P & 0xFFFFFull)  != 0 ? 1u : 0u); }
        if (e < 1) {                                                 // denormalise: shift right, jam into sticky
            const unsigned sh = (1 - e) > 31 ? 31u : static_cast<unsigned>(1 - e);
            const uint32_t lost = m27 & ~(0xFFFFFFFFu << sh);
            m27 = shift(m27, sh, false, false) | (lost != 0 ? 1u : 0u);
            e = 1;
        }
        return fpRoundPack(sign, e, m27);
    }

    // ---- fast path: rounding merged into the final addition ----
    const CarrySave cX = fpMantissaProduct(mx, my, 1ull << 23);      // P + half ulp when P >= 2^47
    const CarrySave cY = fpMantissaProduct(mx, my, 1ull << 22);      // P + half ulp when P <  2^47
    const uint64_t tX = koggeStoneAdd(cX.s, cX.c, 48);
    const uint64_t tY = koggeStoneAdd(cY.s, cY.c, 48);

    uint32_t m;
    int e;
    if (top) {
        m = static_cast<uint32_t>(tX >> 24);
        if ((tX & 0xFFFFFFull) == 0) m &= ~1u;                       // tie: round to even
        e = e0 + 1;
    } else {
        m = static_cast<uint32_t>(tY >> 23);
        if ((tY & 0x7FFFFFull) == 0) m &= ~1u;                       // tie: round to even
        e = e0;
        if (m >> 24) { m = 1u << 23; e++; }                          // 1.11..1 rounded up to 10.00..0
    }
    return signBit | (static_cast<uint32_t>(e) << 23) | (m & 0x7FFFFFu);
}

// ======================================================================
// Floating-point divider (binary32): one Goldschmidt iteration, built for low latency.
//
//   sign      : sa xor sb
//   exponent  : ea - eb + 127   (-1 when the mantissa quotient is below 1)
//   mantissa  : q = x / y with x = mx / 2^23 and y = my / 2^23, both in [1,2), so q is in (0.5, 2)
//
//   Goldschmidt scales numerator and denominator by the same factors until the
//   denominator reaches 1:  N0 = x*K, D0 = y*K, F = 2 - D0, N1 = N0*F ~ x/y.
//
//   1. Seed K ~ 1/y: a 64-entry ROM with one linear piece per interval of y
//      (fpRecipSeed), accurate to about 14 bits. N0 and D0 are two independent
//      multiplies, so hardware runs them in parallel.
//   2. One iteration. With eps = 1 - D0 (|eps| < 2^-13) the update is
//      N1 = N0 + N0*eps, a multiply with a short (~16-bit) operand. The error
//      after it is about eps^2 (< 2^-27): more than the 24 bits needed. D1 is not
//      needed, because this is the last iteration.
//   3. Exact-remainder correction (this makes the result correctly rounded).
//      Qt ~ q*2^s (s = 24, or 25 when q < 1) is within one unit of floor(q*2^s).
//      R0 = mx*2^s - Qt*my is formed exactly (one more multiply), and
//      R0 < 0 -> quotient = Qt - 1,   R0 >= my -> Qt + 1,   else Qt.
//      The final remainder R1 in [0, my) gives the sticky bit (R1 != 0).
//      In hardware R0, R0 + my and R0 - my are formed in parallel from the
//      carry-save product; the model derives them from R0.
//   4. Rounding merged into the correction. Rounding to nearest keeps
//      (Qf + 1) >> 1 (Qf = the 25-bit quotient: 24 bits + guard). That value for
//      Qf = Qt-1, Qt, Qt+1 is ready before the remainder is, so the remainder
//      only selects one of three candidates. An exact tie (guard set, R1 == 0)
//      clears the LSB (round to even). Rounding up can carry out of the mantissa
//      and bump the exponent.
//
// Results whose exponent could leave 1..254 take the slow path of fmul
// (shift right with a sticky bit, then fpRoundPack).
// Special values: NaN operands propagate (first NaN, quieted); 0/0 and Inf/Inf
// give the default NaN 0x7FC00000; x/0 gives +-Inf (no divByZero, no exception);
// Inf/x gives +-Inf; 0/x and x/Inf give +-0.
// ======================================================================

// Reciprocal seed: K ~ 1/y as an integer with 22 fractional bits.
// y = my / 2^23 is cut into 64 intervals by its top 6 fraction bits. Inside interval A (y between
// A/64 and (A+1)/64) the chord of 1/y is 64(A+1-f)/(A(A+1)), f = position in the interval, 0..1.
// Lowering it by half of its maximum gap to the curve, 16/((2A+1)A(A+1)), makes the error two-sided.
// ROM contents are generated once with integer arithmetic:
//     c0 = 64/A - 16/((2A+1)A(A+1))   (22 fractional bits)      c1 = 64/(A(A+1))   (24 fractional bits)
// and K = c0 - c1*f, with f taken from the next 10 bits of my.
uint32_t ALU::fpRecipSeed(uint32_t my) {
    struct Rom {
        uint32_t c0[64];
        uint32_t c1[64];
        Rom() {
            for (int i = 0; i < 64; i++) {
                const uint64_t A   = 64 + i;
                const uint64_t den = (2 * A + 1) * A * (A + 1);
                const uint64_t num = 64 * (2 * A + 1) * (A + 1) - 16;
                c0[i] = static_cast<uint32_t>(((num << 22) + den / 2) / den);
                c1[i] = static_cast<uint32_t>(((64ull << 24) + A * (A + 1) / 2) / (A * (A + 1)));
            }
        }
    };
    static const Rom rom;
    const unsigned idx = (my >> 17) & 0x3Fu;                          // top 6 bits below the leading 1
    const uint64_t f   = (my >> 7) & 0x3FFu;                          // next 10 bits: position in the interval
    return rom.c0[idx] - static_cast<uint32_t>((rom.c1[idx] * f) >> 12);
}

uint32_t ALU::fdiv(uint32_t a, uint32_t b) {
    const uint64_t M48 = (1ull << 48) - 1;
    const uint32_t magA = a & 0x7FFFFFFFu, magB = b & 0x7FFFFFFFu;
    const bool sign = ((a ^ b) >> 31) != 0;
    const uint32_t signBit = sign ? 0x80000000u : 0u;

    // ---- special values ----
    if (magA > 0x7F800000u) return a | 0x00400000u;                  // NaN: propagate the first NaN operand, quieted
    if (magB > 0x7F800000u) return b | 0x00400000u;
    const bool aInf = (magA == 0x7F800000u), bInf = (magB == 0x7F800000u);
    if ((aInf && bInf) || (magA == 0 && magB == 0)) return 0x7FC00000u;   // Inf/Inf and 0/0 are invalid: default NaN
    if (aInf) return signBit | 0x7F800000u;                          // Inf / x
    if (bInf) return signBit;                                        // x / Inf
    if (magB == 0) return signBit | 0x7F800000u;                     // x / 0: +-Inf, not an exception here
    if (magA == 0) return signBit;                                   // 0 / x

    // ---- unpack; a subnormal operand is normalised so its leading 1 sits at bit 23 ----
    const FpOperand x = fpUnpack(a, false), y = fpUnpack(b, false);
    const int lzx = fpLeadingZeros(x.mant, 24), lzy = fpLeadingZeros(y.mant, 24);
    const uint32_t mx = x.mant << lzx, my = y.mant << lzy;
    const bool below1 = mx < my;                                     // q < 1
    const int s = below1 ? 25 : 24;                                  // quotient scale: floor(q * 2^s) has 25 bits
    const int e = (x.exp - lzx) - (y.exp - lzy) + 127 - (below1 ? 1 : 0);   // exponent field of the quotient

    // ---- 1. seed and scaling: N0 = x*K, D0 = y*K (independent multiplies) ----
    const uint32_t K = fpRecipSeed(my);                              // 22 fractional bits
    const CarrySave cn = fpMantissaProduct(mx, K, 0);
    const CarrySave cd = fpMantissaProduct(my, K, 0);
    const uint64_t Pn = koggeStoneAdd(cn.s, cn.c, 48);               // x*K with 45 fractional bits
    const uint64_t Pd = koggeStoneAdd(cd.s, cd.c, 48);               // y*K, close to 1.0

    // ---- 2. one iteration: N1 = N0 * (2 - D0) = N0 + N0*eps, eps = 1 - D0 ----
    uint64_t E = koggeStoneAdd(1ull << 45, (~Pd) & M48, 48, true);   // 2^45 - Pd: eps, signed 48-bit
    const bool epsNeg = ((E >> 47) & 1u) != 0;
    if (epsNeg) E = koggeStoneAdd((~E) & M48, 0, 48, true);          // |eps|
    const uint32_t epsMag = static_cast<uint32_t>(E >> 15);          // 30 fractional bits, below 2^18
    const uint32_t N0 = static_cast<uint32_t>(Pn >> 15);             // 30 fractional bits
    const CarrySave cm = fpMantissaProduct(N0, epsMag, 0);
    const uint64_t corr = koggeStoneAdd(cm.s, cm.c, 48) >> 30;       // N0*|eps|, 30 fractional bits
    const uint64_t N1 = epsNeg ? koggeStoneAdd(N0, (~corr) & M48, 48, true)    // N0 - corr
                               : koggeStoneAdd(N0, corr, 48);                  // N0 + corr

    // ---- 3. quotient estimate and exact remainder ----
    const uint32_t Qt = static_cast<uint32_t>(N1 >> (30 - s));       // ~ q * 2^s, within one unit of the floor
    const CarrySave cz = fpMantissaProduct(Qt, my, 0);
    const uint64_t Z   = koggeStoneAdd(cz.s, cz.c, 48);              // Qt*my  (mod 2^48)
    const uint64_t mxs = (static_cast<uint64_t>(mx) << s) & M48;
    const uint64_t R0  = koggeStoneAdd(mxs, (~Z) & M48, 48, true);   // mx*2^s - Qt*my: small and signed
    const uint64_t Rp  = koggeStoneAdd(R0, my, 48);                  // R0 + my
    const uint64_t Rm  = koggeStoneAdd(R0, (~static_cast<uint64_t>(my)) & M48, 48, true);   // R0 - my
    int adj;
    uint64_t R1;
    if ((R0 >> 47) & 1u)         { adj = -1; R1 = Rp; }              // estimate too big
    else if (!((Rm >> 47) & 1u)) { adj = +1; R1 = Rm; }              // estimate too small
    else                         { adj =  0; R1 = R0; }
    const uint32_t Qf = static_cast<uint32_t>(static_cast<int>(Qt) + adj);   // floor(q * 2^s), 25 bits
    const bool exact = (R1 == 0);                                    // no sticky bit

    // ---- slow path: the result may be subnormal or overflow ----
    if (e < 1 || e > 253) {
        uint32_t m27 = (Qf << 2) | (exact ? 0u : 1u);                // hidden bit at 26, guard = Qf bit 0, sticky below
        int ee = e;
        if (ee < 1) {                                                // denormalise: shift right, jam into sticky
            const unsigned sh = (1 - ee) > 31 ? 31u : static_cast<unsigned>(1 - ee);
            const uint32_t lost = m27 & ~(0xFFFFFFFFu << sh);
            m27 = shift(m27, sh, false, false) | (lost != 0 ? 1u : 0u);
            ee = 1;
        }
        return fpRoundPack(sign, ee, m27);
    }

    // ---- fast path: the remainder only selects among rounded candidates ----
    const uint32_t cand[3] = { Qt >> 1, (Qt + 1) >> 1, (Qt + 2) >> 1 };      // (Qf + 1) >> 1 for adj = -1, 0, +1
    uint32_t m = cand[adj + 1];
    if ((Qf & 1u) && exact) m &= ~1u;                                // exact tie: round to even
    int ee = e;
    if (m >> 24) { m = 1u << 23; ee++; }                             // 1.11..1 rounded up to 10.00..0
    return signBit | (static_cast<uint32_t>(ee) << 23) | (m & 0x7FFFFFu);
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

    if (s.isFloat) {                             // add/sub/mul/div on float registers: no flags, no divByZero
        if      (s.isAdd) r.value = fadd(a, b);
        else if (s.isSub) r.value = fsub(a, b);
        else if (s.isMul) r.value = fmul(a, b);
        else if (s.isDiv) r.value = fdiv(a, b);
    } else if (s.isAdd || s.isLd || s.isSt) {    // ld/st: effective address = base + offset
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