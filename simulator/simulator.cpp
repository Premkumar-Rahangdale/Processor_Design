// ============================================================================
//  RISC201 CLI Simulator
//  ----------------------------------------------------------------------------
//  An instruction-level simulator for the RISC201 ISA (Q1 design reference),
//  with:
//    * a built-in assembler (same encoding as assembler.cpp)
//    * a loader for the binary-text files produced by assembler.exe
//    * a disassembler (same output style as disassembler.cpp)
//    * an interactive debugger (step / run / breakpoints / registers / memory)
//    * a horizontal-microprogram view (microPC, stage, control signals)
//      for the 4-stage and the 6-stage control unit (Q1(b))
//    * a pipeline timing model (4-stage vs 6-stage, with/without forwarding)
//
//  Build :  g++ -std=c++17 -O2 -o risc201_sim risc201_sim.cpp
//  Run   :  ./risc201_sim programs/factorial.asm
//
//  File layout (read top to bottom):
//    1. ISA definition        (opcodes, formats, field extraction)
//    2. MachineState          (registers, memory, flags, pc)
//    3. Assembler / loader / disassembler
//    4. CPU                   (fetch-decode-execute, exceptions)
//    5. Microprogram view     (Q1(b))
//    6. Pipeline timing model (4-stage vs 6-stage)
//    7. Shell                 (the command-line interface)
// ============================================================================
#include <cstdarg>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
#define SIM_ISATTY(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#define SIM_ISATTY(f) isatty(fileno(f))
#endif

// ----------------------------------------------------------------------------
//  Small UI helpers (colour only when writing to a real terminal)
// ----------------------------------------------------------------------------
namespace ui {
bool color = false;
std::string paint(const char* code, const std::string& s) {
    if (!color) return s;
    return std::string("\x1b[") + code + "m" + s + "\x1b[0m";
}
std::string bold(const std::string& s)    { return paint("1", s); }
std::string dim(const std::string& s)     { return paint("2", s); }
std::string red(const std::string& s)     { return paint("31", s); }
std::string green(const std::string& s)   { return paint("32", s); }
std::string yellow(const std::string& s)  { return paint("33", s); }
std::string cyan(const std::string& s)    { return paint("36", s); }
std::string magenta(const std::string& s) { return paint("35", s); }
}  // namespace ui

static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static std::string upper(std::string s) {
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
static std::vector<std::string> split(const std::string& s) {
    std::istringstream ss(s);
    std::vector<std::string> v;
    std::string t;
    while (ss >> t) v.push_back(t);
    return v;
}
static std::string toBin(uint32_t v, int bits) {
    std::string s;
    for (int i = bits - 1; i >= 0; --i) s += ((v >> i) & 1) ? '1' : '0';
    return s;
}
static bool parseInt(const std::string& t, long long& v) {
    if (t.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (t[i] == '-' || t[i] == '+') { neg = (t[i] == '-'); ++i; }
    if (i >= t.size()) return false;
    int base = 10;
    if (t.size() > i + 1 && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) { base = 16; i += 2; }
    else if (t.size() > i + 1 && t[i] == '0' && (t[i + 1] == 'b' || t[i + 1] == 'B')) { base = 2; i += 2; }
    if (i >= t.size()) return false;
    long long r = 0;
    for (; i < t.size(); ++i) {
        char ch = t[i];
        int dgt;
        if (isdigit((unsigned char)ch)) dgt = ch - '0';
        else if (base == 16 && isxdigit((unsigned char)ch)) dgt = tolower(ch) - 'a' + 10;
        else return false;
        if (dgt >= base) return false;
        r = r * base + dgt;
        if (r > (1LL << 40)) return false;
    }
    v = neg ? -r : r;
    return true;
}

// ============================================================================
//  1. ISA DEFINITION  (mirrors isa.hpp / assembler.cpp / disassembler.cpp)
//
//  32-bit instruction, 5-bit opcode in bits 31..27
//
//   ALU3 / MEM : | op(5) | I(1) | rd(5) | rs1(5) | imm16   or  rs2(5)+11 zeros |
//   mov/not/.. : | op(5) | I(1) | rd(5) | 00000  | imm16   or  rs2(5)+11 zeros |
//   cmp        : | op(5) | I(1) | 00000 | rs1(5) | imm16   or  rs2(5)+11 zeros |
//   branches   : | op(5) | signed 27-bit word offset (target = pc + offset)    |
//   nop / ret  : | op(5) | 27 zeros                                            |
// ============================================================================
constexpr unsigned OPCODE_SHIFT = 27;
constexpr uint32_t OPCODE_MASK = 0x1F;

enum class Opcode : uint8_t {
    Add = 0b00001, Sub = 0b00010, Mul = 0b00011, Div = 0b00100, Mod = 0b00101,
    Cmp = 0b00110, And = 0b00111, Or = 0b01000, Not = 0b01001, Xor = 0b01010,
    Mov = 0b01011, Movu = 0b01100, Movh = 0b01101,
    Lsl = 0b01110, Lsr = 0b01111, Asr = 0b10000,
    Nop = 0b10001, Ld = 0b10010, St = 0b10011,
    Beq = 0b10100, Bgt = 0b10101, Bsm = 0b10110, B = 0b10111,
    Call = 0b11000, Ret = 0b11001
};

// Operand format of each instruction
enum class Fmt { ALU3, UNARY, CMP, MEM, BRANCH, NONE };

struct OpInfo {
    Opcode op;
    const char* name;
    Fmt fmt;
    const char* aluCtrl;  // 5-bit ALU control code from Q1(c) table (nullptr = ALU unused)
};

static const OpInfo OPTABLE[] = {
    {Opcode::Add, "add", Fmt::ALU3, "00001"},   {Opcode::Sub, "sub", Fmt::ALU3, "00010"},
    {Opcode::Mul, "mul", Fmt::ALU3, "00011"},   {Opcode::Div, "div", Fmt::ALU3, "00100"},
    {Opcode::Mod, "mod", Fmt::ALU3, "00101"},   {Opcode::Cmp, "cmp", Fmt::CMP, "00110"},
    {Opcode::And, "and", Fmt::ALU3, "00111"},   {Opcode::Or, "or", Fmt::ALU3, "01000"},
    {Opcode::Not, "not", Fmt::UNARY, "01010"},  {Opcode::Xor, "xor", Fmt::ALU3, "01001"},
    {Opcode::Mov, "mov", Fmt::UNARY, "01110"},  {Opcode::Movu, "movu", Fmt::UNARY, "01110"},
    {Opcode::Movh, "movh", Fmt::UNARY, "01110"},{Opcode::Lsl, "lsl", Fmt::ALU3, "01011"},
    {Opcode::Lsr, "lsr", Fmt::ALU3, "01100"},   {Opcode::Asr, "asr", Fmt::ALU3, "01101"},
    {Opcode::Nop, "nop", Fmt::NONE, nullptr},   {Opcode::Ld, "ld", Fmt::MEM, "00001"},
    {Opcode::St, "st", Fmt::MEM, "00001"},      {Opcode::Beq, "beq", Fmt::BRANCH, "00001"},
    {Opcode::Bgt, "bgt", Fmt::BRANCH, "00001"}, {Opcode::Bsm, "bsm", Fmt::BRANCH, "00001"},
    {Opcode::B, "b", Fmt::BRANCH, "00001"},     {Opcode::Call, "call", Fmt::BRANCH, "00001"},
    {Opcode::Ret, "ret", Fmt::NONE, nullptr},
};

static const OpInfo* findOp(uint32_t raw5) {
    for (const OpInfo& o : OPTABLE)
        if ((uint32_t)o.op == raw5) return &o;
    return nullptr;  // opcode 0 and 26..31 are unused -> invalid
}
static const OpInfo* findOpByName(const std::string& n) {
    for (const OpInfo& o : OPTABLE)
        if (n == o.name) return &o;
    return nullptr;
}

static int32_t signExtend(uint32_t v, int bits) {
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

struct Instr {
    uint32_t raw = 0;
    const OpInfo* info = nullptr;  // nullptr => invalid opcode
    bool imm = false;
    int rd = 0, rs1 = 0, rs2 = 0;
    int32_t imm16 = 0;   // sign-extended 16-bit immediate
    int32_t off27 = 0;   // sign-extended 27-bit branch offset
    bool valid() const { return info != nullptr; }
};

static Instr decode(uint32_t w) {
    Instr d;
    d.raw = w;
    d.info = findOp((w >> OPCODE_SHIFT) & OPCODE_MASK);
    d.imm = (w >> 26) & 1;
    d.rd = (w >> 21) & 0x1F;
    d.rs1 = (w >> 16) & 0x1F;
    d.rs2 = (w >> 11) & 0x1F;
    d.imm16 = signExtend(w & 0xFFFF, 16);
    d.off27 = signExtend(w & 0x7FFFFFF, 27);
    return d;
}

// Registers a given instruction reads / writes (used by the pipeline model)
static void deps(const Instr& d, int& dest, std::vector<int>& src) {
    dest = -1;
    src.clear();
    if (!d.valid()) return;
    auto S = [&](int r) { if (r != 0) src.push_back(r); };
    auto D = [&](int r) { if (r != 0) dest = r; };
    switch (d.info->fmt) {
        case Fmt::ALU3: S(d.rs1); if (!d.imm) S(d.rs2); D(d.rd); break;
        case Fmt::UNARY:
            if (!d.imm) S(d.rs2);
            if (d.info->op == Opcode::Movh) S(d.rd);  // keeps the low half of rd
            D(d.rd);
            break;
        case Fmt::CMP: S(d.rs1); if (!d.imm) S(d.rs2); break;
        case Fmt::MEM:
            S(d.rs1); if (!d.imm) S(d.rs2);
            if (d.info->op == Opcode::Ld) D(d.rd); else S(d.rd);  // st reads rd (the data)
            break;
        case Fmt::BRANCH: if (d.info->op == Opcode::Call) dest = 1; break;  // call writes R1
        case Fmt::NONE: if (d.info->op == Opcode::Ret) S(1); break;         // ret reads R1
    }
}

// ============================================================================
//  2. MACHINE STATE  (from MachineState.cpp)
// ============================================================================
constexpr int Num_Registers = 32;
constexpr int Memory_Words = 4096;

struct Flags {
    bool zero = false;
    bool neg = false;
    bool over = false;
    bool carry = false;

    constexpr bool eq() const { return zero; }
    constexpr bool gt() const { return !zero && (neg == over); }
    constexpr bool lt() const { return neg != over; }
};

struct MachineState {
    std::array<std::uint32_t, Num_Registers> Reg{};
    std::vector<std::uint32_t> memory = std::vector<std::uint32_t>(Memory_Words, 0u);
    std::uint32_t pc = 0;
    Flags flags{};
    void reset() {
        Reg.fill(0);
        std::fill(memory.begin(), memory.end(), 0);
        pc = 0;
        flags = Flags{};
    }
};

static const char* regRole(int r) {
    switch (r) {
        case 0: return "gnd"; case 1: return "ra"; case 2: return "sp";
        case 3: return "fp";  case 4: return "k0"; case 5: return "k1";
        default: return "";
    }
}
static std::string flagStr(const Flags& f) {
    return fmt("Z=%d N=%d V=%d C=%d", f.zero, f.neg, f.over, f.carry);
}

// ============================================================================
//  3. ASSEMBLER / LOADER / DISASSEMBLER
// ============================================================================
struct Program {
    std::vector<uint32_t> words;
    std::map<std::string, int> labels;               // name (no dot) -> address
    std::map<int, std::vector<std::string>> labelsAt;  // address -> names
    void indexLabels() {
        labelsAt.clear();
        for (auto& kv : labels) labelsAt[kv.second].push_back(kv.first);
    }
};
struct AsmError { int line; std::string msg; };

static std::string normLabel(std::string t) {
    while (!t.empty() && t.back() == ':') t.pop_back();
    if (!t.empty() && t[0] == '.') t.erase(0, 1);
    return t;
}

static int parseReg(const std::string& tok) {
    std::string t = lower(tok);
    if (t == "gnd" || t == "zero") return 0;
    if (t == "ra") return 1;
    if (t == "sp") return 2;
    if (t == "fp") return 3;
    if (t.size() >= 2 && t.size() <= 3 && t[0] == 'r') {
        long long v;
        if (parseInt(t.substr(1), v) && v >= 0 && v < 32 && isdigit((unsigned char)t[1])) return (int)v;
    }
    return -1;
}

static std::string stripComment(std::string s) {
    size_t p = s.find_first_of(";#");
    if (p != std::string::npos) s.erase(p);
    p = s.find("//");
    if (p != std::string::npos) s.erase(p);
    return s;
}

// ---- Assembler: text -> machine words --------------------------------------
static bool assemble(const std::string& text, Program& out, std::vector<AsmError>& errs) {
    struct Pend { int line; std::string text; };
    std::vector<Pend> pend;
    Program p;
    std::istringstream in(text);
    std::string raw;
    int ln = 0;
    // Pass 1: collect labels (".name:" or "name:") and instruction lines
    while (std::getline(in, raw)) {
        ++ln;
        std::string s = trim(stripComment(raw));
        while (!s.empty()) {
            size_t sp = s.find_first_of(" \t");
            std::string tok = s.substr(0, sp);
            if (tok.back() != ':') break;
            std::string key = normLabel(tok);
            if (key.empty()) errs.push_back({ln, "empty label"});
            else if (p.labels.count(key)) errs.push_back({ln, "duplicate label '" + key + "'"});
            else p.labels[key] = (int)pend.size();
            s = (sp == std::string::npos) ? "" : trim(s.substr(sp));
        }
        if (!s.empty()) pend.push_back({ln, s});
    }
    // Pass 2: encode
    for (size_t idx = 0; idx < pend.size(); ++idx) {
        const Pend& pd = pend[idx];
        std::string line = pd.text;
        for (char& c : line) if (c == ',') c = ' ';
        std::vector<std::string> tk = split(line);
        auto fail = [&](const std::string& m) { errs.push_back({pd.line, m + "   [" + pd.text + "]"}); };
        const OpInfo* oi = findOpByName(lower(tk[0]));
        if (!oi) { fail("unknown instruction '" + tk[0] + "'"); continue; }
        size_t nops = tk.size() - 1;

        auto enc = [&](bool imm, int rd, int rs1, int rs2, int32_t i16) -> uint32_t {
            return ((uint32_t)oi->op << 27) | ((uint32_t)imm << 26) | ((uint32_t)rd << 21) |
                   ((uint32_t)rs1 << 16) | (imm ? ((uint32_t)i16 & 0xFFFF) : ((uint32_t)rs2 << 11));
        };
        auto needReg = [&](const std::string& t, int& r) -> bool {
            r = parseReg(t);
            if (r < 0) { fail("expected a register (r0..r31), got '" + t + "'"); return false; }
            return true;
        };
        // register or 16-bit immediate
        auto regOrImm = [&](const std::string& t, bool& isImm, int& reg, int32_t& imm) -> bool {
            int r = parseReg(t);
            if (r >= 0) { isImm = false; reg = r; return true; }
            long long v;
            if (!parseInt(t, v)) { fail("bad operand '" + t + "' (expected register or number)"); return false; }
            if (v < -32768 || v > 65535) { fail("immediate " + t + " does not fit in 16 bits"); return false; }
            isImm = true;
            imm = (int32_t)(int16_t)(v & 0xFFFF);
            return true;
        };

        uint32_t w = 0;
        bool good = true;
        bool isImm = false;
        int rd = 0, rs1 = 0, rs2 = 0;
        int32_t imm = 0;
        switch (oi->fmt) {
            case Fmt::NONE:
                if (nops != 0) { fail(std::string(oi->name) + " takes no operands"); good = false; }
                else w = (uint32_t)oi->op << 27;
                break;
            case Fmt::BRANCH: {
                if (nops != 1) { fail(std::string(oi->name) + " takes one operand (label or offset)"); good = false; break; }
                std::string key = normLabel(tk[1]);
                long long off;
                if (p.labels.count(key)) off = p.labels[key] - (long long)idx;
                else if (!parseInt(tk[1], off)) { fail("undefined label '" + tk[1] + "'"); good = false; break; }
                w = ((uint32_t)oi->op << 27) | ((uint32_t)off & 0x7FFFFFF);
                break;
            }
            case Fmt::ALU3:
                if (nops != 3) { fail(std::string(oi->name) + " takes 3 operands: rd rs1 rs2|imm"); good = false; break; }
                good = needReg(tk[1], rd) && needReg(tk[2], rs1) && regOrImm(tk[3], isImm, rs2, imm);
                if (good) w = enc(isImm, rd, rs1, rs2, imm);
                break;
            case Fmt::UNARY:
                if (nops != 2) { fail(std::string(oi->name) + " takes 2 operands: rd rs|imm"); good = false; break; }
                good = needReg(tk[1], rd) && regOrImm(tk[2], isImm, rs2, imm);
                if (good) w = enc(isImm, rd, 0, rs2, imm);
                break;
            case Fmt::CMP:
                if (nops != 2) { fail("cmp takes 2 operands: rs1 rs2|imm"); good = false; break; }
                good = needReg(tk[1], rs1) && regOrImm(tk[2], isImm, rs2, imm);
                if (good) w = enc(isImm, 0, rs1, rs2, imm);
                break;
            case Fmt::MEM:
                if (nops == 3) {  // ld rd rs1 imm|rs2
                    good = needReg(tk[1], rd) && needReg(tk[2], rs1) && regOrImm(tk[3], isImm, rs2, imm);
                } else if (nops == 2) {  // ld rd imm[rs1]   (disassembler style)
                    good = needReg(tk[1], rd);
                    if (good) {
                        const std::string& m = tk[2];
                        size_t lb = m.find_first_of("[(");
                        size_t rb = m.find_first_of("])");
                        if (lb == std::string::npos || rb == std::string::npos || rb < lb) {
                            fail("expected imm[rs1] memory operand"); good = false;
                        } else {
                            std::string off = trim(m.substr(0, lb)), base = trim(m.substr(lb + 1, rb - lb - 1));
                            long long v = 0;
                            if (!off.empty() && !parseInt(off, v)) { fail("bad offset '" + off + "'"); good = false; }
                            else if (v < -32768 || v > 32767) { fail("offset does not fit in 16 bits"); good = false; }
                            else if (!needReg(base, rs1)) good = false;
                            else { isImm = true; imm = (int32_t)v; }
                        }
                    }
                } else { fail(std::string(oi->name) + " takes: rd rs1 imm   (or: rd imm[rs1])"); good = false; }
                if (good) w = enc(isImm, rd, rs1, rs2, imm);
                break;
        }
        if (good) p.words.push_back(w);
        else p.words.push_back(0);  // keep addresses aligned while reporting more errors
    }
    p.indexLabels();
    if (!errs.empty()) return false;
    out = p;
    return true;
}

// ---- Loader for the binary-text output of assembler.exe ----------------------
// Their file has one line per instruction (32 chars of 0/1) and label lines
// (".name:"). Their branch offsets are counted in *lines* (label lines
// included), so we convert them to real instruction offsets here.
static bool looksBinary(const std::string& text) {
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        l = trim(l);
        if (l.empty()) continue;
        if (l[0] == '.' && l.back() == ':') continue;  // label line, keep looking
        if (l.size() != 32) return false;
        return l.find_first_not_of("01") == std::string::npos;
    }
    return false;
}

static bool loadBinaryText(const std::string& text, Program& out, std::vector<AsmError>& errs) {
    struct Entry { bool label; std::string name; uint32_t w; int line; };
    std::vector<Entry> e;
    std::istringstream in(text);
    std::string l;
    int ln = 0;
    while (std::getline(in, l)) {
        ++ln;
        l = trim(l);
        if (l.empty()) continue;
        if (l[0] == '.') { e.push_back({true, normLabel(l), 0, ln}); continue; }
        std::string tok = split(l)[0];
        if (tok.size() != 32 || tok.find_first_not_of("01") != std::string::npos) {
            errs.push_back({ln, "expected 32 binary digits or a .label: line"});
            continue;
        }
        e.push_back({false, "", (uint32_t)std::stoul(tok, nullptr, 2), ln});
    }
    if (!errs.empty()) return false;
    std::vector<int> before(e.size() + 1, 0);  // instructions strictly before entry i
    for (size_t i = 0; i < e.size(); ++i) before[i + 1] = before[i] + (e[i].label ? 0 : 1);
    Program p;
    for (size_t i = 0; i < e.size(); ++i) {
        if (e[i].label) { p.labels[e[i].name] = before[i]; continue; }
        uint32_t w = e[i].w;
        Instr d = decode(w);
        if (d.valid() && d.info->fmt == Fmt::BRANCH) {
            long long target = (long long)i + d.off27;  // target *line*
            if (target >= 0 && target <= (long long)e.size()) {
                int newOff = before[target] - before[i];
                w = (w & ~0x7FFFFFFu) | ((uint32_t)newOff & 0x7FFFFFF);
            }
        }
        p.words.push_back(w);
    }
    p.indexLabels();
    out = p;
    return true;
}

// ---- Disassembler (same text style as disassembler.cpp) ------------------------
static std::string disasm(const Instr& d) {
    if (!d.valid()) return "invalid opcode (" + toBin((d.raw >> 27) & 0x1F, 5) + ")";
    std::string n = d.info->name;
    auto R = [](int r) { return "r" + std::to_string(r); };
    switch (d.info->fmt) {
        case Fmt::NONE: return n;
        case Fmt::BRANCH: return n + " " + std::to_string(d.off27);
        case Fmt::MEM:
            if (d.imm) return n + " " + R(d.rd) + " " + std::to_string(d.imm16) + "[" + R(d.rs1) + "]";
            return n + " " + R(d.rd) + " " + R(d.rs1) + " " + R(d.rs2);
        case Fmt::UNARY: return n + " " + R(d.rd) + " " + (d.imm ? std::to_string(d.imm16) : R(d.rs2));
        case Fmt::CMP: return n + " " + R(d.rs1) + " " + (d.imm ? std::to_string(d.imm16) : R(d.rs2));
        case Fmt::ALU3:
            return n + " " + R(d.rd) + " " + R(d.rs1) + " " + (d.imm ? std::to_string(d.imm16) : R(d.rs2));
    }
    return n;
}
// Same, with the branch target resolved to a label when possible
static std::string disasmSym(const Instr& d, int pc, const Program& prog) {
    std::string s = disasm(d);
    if (d.valid() && d.info->fmt == Fmt::BRANCH) {
        int t = pc + d.off27;
        auto it = prog.labelsAt.find(t);
        s += "   ; -> " + fmt("%04d", t);
        if (it != prog.labelsAt.end()) s += " (." + it->second[0] + ")";
    }
    return s;
}
static std::string fieldBin(const Instr& d) {
    uint32_t w = d.raw;
    if (!d.valid() || d.info->fmt == Fmt::BRANCH || d.info->fmt == Fmt::NONE)
        return toBin(w >> 27, 5) + " " + toBin(w & 0x7FFFFFF, 27);
    return toBin(w >> 27, 5) + " " + toBin((w >> 26) & 1, 1) + " " + toBin(d.rd, 5) + " " +
           toBin(d.rs1, 5) + " " + toBin(w & 0xFFFF, 16);
}

// ============================================================================
//  4. CPU : fetch - decode - execute
//
//  Design decisions where the ISA document says "to be defined":
//   * Instruction memory and data memory are separate (Harvard); data memory
//     is MachineState::memory (4096 words, word-addressed).
//   * 16-bit immediates are sign-extended.   mul keeps the low 32 bits.
//   * mov  rd,x  : rd = x                    (x = sign-extended imm16 or rs)
//     movu rd,x  : rd = x << 16              (load upper half, clear lower)
//     movh rd,x  : rd[31:16] = x, rd[15:0] kept
//   * Branch target = (address of the branch) + offset, offset in words.
//   * Only cmp updates the flags. call: R1 = pc+1. ret: pc = R1.
//   * ret with R1 == 0 means "return from top level" -> machine halts.
//   * Exceptions (halt the machine, R4 = cause, R5 = faulting pc):
//        1 = divide by zero, 2 = data-memory fault, 3 = invalid opcode,
//        4 = branch target outside the program
// ============================================================================
struct TraceRec {          // one executed instruction, kept for the pipeline model
    uint32_t pc = 0;
    int dest = -1;
    std::vector<int> src;
    bool isLoad = false, isStore = false, isCtrl = false, isCond = false;
    bool taken = false;    // control transfer actually redirected the PC
    std::string text;
};

struct StepResult {
    bool executed = false;
    uint32_t pc = 0;
    Instr d;
    std::string text;
    std::vector<std::string> changes;
    bool taken = false;
};

struct CPU {
    MachineState st;
    Program prog;
    bool loaded = false;
    bool halted = false;
    std::string haltReason;
    bool exception = false;
    uint64_t instrCount = 0;
    std::vector<TraceRec> trace;
    static constexpr size_t TRACE_CAP = 200000;
    std::set<int> breakpoints;
    std::set<int> changedRegs;

    void reset() {
        st.reset();
        st.Reg[2] = Memory_Words;  // stack pointer starts at the top of data memory
        halted = false;
        exception = false;
        haltReason.clear();
        instrCount = 0;
        trace.clear();
        changedRegs.clear();
    }

    void trap(int cause, uint32_t pc, const std::string& msg) {
        st.Reg[4] = cause;
        st.Reg[5] = pc;
        halted = true;
        exception = true;
        haltReason = "EXCEPTION: " + msg + fmt(" at pc=%04u   (R4=cause %d, R5=faulting pc)", pc, cause);
    }

    StepResult step() {
        StepResult r;
        if (halted) return r;
        if (st.pc >= prog.words.size()) {
            halted = true;
            haltReason = "program ended (PC ran past the last instruction)";
            return r;
        }
        const uint32_t pc = st.pc;
        Instr d = decode(prog.words[pc]);
        r.executed = true;
        r.pc = pc;
        r.d = d;
        r.text = disasmSym(d, (int)pc, prog);
        auto regsBefore = st.Reg;
        Flags flagsBefore = st.flags;
        changedRegs.clear();

        uint32_t nextPc = pc + 1;
        bool ok = true, taken = false;

        if (!d.valid()) {
            trap(3, pc, "invalid opcode");
            ok = false;
        } else {
            const uint32_t A = st.Reg[d.rs1];
            const uint32_t B = d.imm ? (uint32_t)d.imm16 : st.Reg[d.rs2];
            auto W = [&](int idx, uint32_t v) { if (idx != 0) st.Reg[idx] = v; };  // R0 is read-only
            auto memAddr = [&](int64_t& addr) -> bool {
                addr = (int64_t)(int32_t)(A + B);
                if (addr < 0 || addr >= Memory_Words) {
                    trap(2, pc, fmt("data memory fault (address %lld)", (long long)addr));
                    return false;
                }
                return true;
            };
            switch (d.info->op) {
                case Opcode::Add: W(d.rd, A + B); break;
                case Opcode::Sub: W(d.rd, A - B); break;
                case Opcode::Mul: W(d.rd, A * B); break;
                case Opcode::Div:
                case Opcode::Mod: {
                    int32_t a = (int32_t)A, b = (int32_t)B;
                    if (b == 0) { trap(1, pc, "division by zero"); ok = false; break; }
                    bool isDiv = d.info->op == Opcode::Div;
                    if (a == INT32_MIN && b == -1) W(d.rd, isDiv ? (uint32_t)a : 0u);
                    else W(d.rd, (uint32_t)(isDiv ? a / b : a % b));
                    break;
                }
                case Opcode::Cmp: {
                    uint32_t res = A - B;
                    st.flags.zero = (res == 0);
                    st.flags.neg = (res >> 31) & 1;
                    st.flags.over = (((A ^ B) & (A ^ res)) >> 31) & 1;
                    st.flags.carry = (A >= B);  // no borrow
                    break;
                }
                case Opcode::And: W(d.rd, A & B); break;
                case Opcode::Or:  W(d.rd, A | B); break;
                case Opcode::Xor: W(d.rd, A ^ B); break;
                case Opcode::Not: W(d.rd, ~B); break;
                case Opcode::Mov: W(d.rd, B); break;
                case Opcode::Movu: W(d.rd, (B & 0xFFFF) << 16); break;
                case Opcode::Movh: W(d.rd, (st.Reg[d.rd] & 0xFFFF) | ((B & 0xFFFF) << 16)); break;
                case Opcode::Lsl: W(d.rd, A << (B & 31)); break;
                case Opcode::Lsr: W(d.rd, A >> (B & 31)); break;
                case Opcode::Asr: W(d.rd, (uint32_t)((int32_t)A >> (B & 31))); break;
                case Opcode::Nop: break;
                case Opcode::Ld: {
                    int64_t a;
                    if (!memAddr(a)) { ok = false; break; }
                    W(d.rd, st.memory[(size_t)a]);
                    break;
                }
                case Opcode::St: {
                    int64_t a;
                    if (!memAddr(a)) { ok = false; break; }
                    st.memory[(size_t)a] = st.Reg[d.rd];
                    r.changes.push_back(fmt("mem[%lld] = %d (0x%X)", (long long)a, (int32_t)st.Reg[d.rd], st.Reg[d.rd]));
                    break;
                }
                case Opcode::Beq: taken = st.flags.eq(); break;
                case Opcode::Bgt: taken = st.flags.gt(); break;
                case Opcode::Bsm: taken = st.flags.lt(); break;
                case Opcode::B: taken = true; break;
                case Opcode::Call: taken = true; W(1, pc + 1); break;
                case Opcode::Ret:
                    if (st.Reg[1] == 0) {
                        halted = true;
                        haltReason = "ret with R1 = 0 (return from top level)";
                        nextPc = pc;
                    } else { taken = true; nextPc = st.Reg[1]; }
                    break;
            }
            if (ok && d.info->fmt == Fmt::BRANCH && taken) {
                long long t = (long long)pc + d.off27;
                if (t < 0 || t > (long long)prog.words.size()) {
                    trap(4, pc, fmt("branch target %lld outside program", t));
                    ok = false;
                } else nextPc = (uint32_t)t;
            }
        }
        if (ok) {
            st.pc = nextPc;
            ++instrCount;
            if (trace.size() < TRACE_CAP) {
                TraceRec t;
                t.pc = pc;
                deps(d, t.dest, t.src);
                t.isLoad = d.info->op == Opcode::Ld;
                t.isStore = d.info->op == Opcode::St;
                t.isCtrl = d.info->fmt == Fmt::BRANCH || d.info->op == Opcode::Ret;
                t.isCond = d.info->op == Opcode::Beq || d.info->op == Opcode::Bgt || d.info->op == Opcode::Bsm;
                t.taken = taken;
                t.text = disasm(d);
                trace.push_back(t);
            }
        }
        r.taken = taken && ok;
        for (int i = 1; i < Num_Registers; ++i)
            if (regsBefore[i] != st.Reg[i]) {
                changedRegs.insert(i);
                r.changes.insert(r.changes.begin(), fmt("r%d = %d (0x%X)", i, (int32_t)st.Reg[i], st.Reg[i]));
            }
        if (flagsBefore.zero != st.flags.zero || flagsBefore.neg != st.flags.neg ||
            flagsBefore.over != st.flags.over || flagsBefore.carry != st.flags.carry)
            r.changes.push_back("flags: " + flagStr(st.flags));
        return r;
    }
};

// ============================================================================
//  5. MICROPROGRAM VIEW  (Q1(b): horizontal microprogrammed control unit)
//  Shows, for one instruction, the micro-instructions the control unit would
//  issue: microPC, pipeline stage, control signals asserted, next microPC.
// ============================================================================
struct MicroStep { int upc; std::string stage, signals, note; int next; };

static std::vector<MicroStep> microprogram(const Instr& d, int stages, bool taken) {
    std::vector<MicroStep> m;
    if (!d.valid()) return m;
    const OpInfo* o = d.info;
    const std::string name = o->name;
    const bool six = (stages == 6);
    std::string opB = d.imm ? "IMM" : "REG";
    int dest;
    std::vector<int> src;
    deps(d, dest, src);

    auto join = [](std::vector<std::string> v) {
        std::string s;
        for (auto& x : v) { if (!s.empty()) s += ' '; s += x; }
        return s;
    };
    std::vector<std::string> ex;
    std::string exNote;
    bool memInEx = !six;  // 4-stage: memory access shares the EX/MEM stage
    switch (o->fmt) {
        case Fmt::ALU3:
            if (o->op == Opcode::Lsl || o->op == Opcode::Lsr || o->op == Opcode::Asr)
                ex.push_back("SHIFT_" + upper(name));
            else ex.push_back("ALU_" + upper(name));
            ex.insert(ex.end(), {"OP_A_SEL=REG", "OP_B_SEL=" + opB});
            exNote = "ALU computes rs1 " + name + " " + (d.imm ? "imm" : "rs2");
            break;
        case Fmt::UNARY:
            ex.push_back(o->op == Opcode::Not ? "ALU_NOT" : "ALU_MOV");
            ex.push_back("OP_B_SEL=" + opB);
            exNote = o->op == Opcode::Movu ? "upper-immediate placement (value << 16)"
                   : o->op == Opcode::Movh ? "replace high half, keep low half" : "operand forwarded to result";
            break;
        case Fmt::CMP:
            ex.insert(ex.end(), {"ALU_CMP", "CMP_UPDATE", "OP_A_SEL=REG", "OP_B_SEL=" + opB});
            exNote = "A - B, set Z/N/V/C flags";
            break;
        case Fmt::MEM:
            ex.insert(ex.end(), {"ALU_ADD", "OP_A_SEL=REG", "OP_B_SEL=" + opB});
            if (memInEx) ex.push_back(o->op == Opcode::Ld ? "MEM_READ" : "MEM_WRITE");
            exNote = memInEx ? "effective address = base + offset, then access data memory"
                             : "effective address = base + offset";
            break;
        case Fmt::BRANCH:
            ex.insert(ex.end(), {"ALU_ADD", "OP_A_SEL=PC", "OP_B_SEL=IMM"});
            if (o->op == Opcode::Beq) ex.push_back("BR_COND=BEQ");
            if (o->op == Opcode::Bgt) ex.push_back("BR_COND=BGT");
            if (o->op == Opcode::Bsm) ex.push_back("BR_COND=BSM");
            if (o->op == Opcode::B || o->op == Opcode::Call) ex.push_back("BR_COND=ALWAYS");
            if (taken) ex.insert(ex.end(), {"PC_SRC=TARGET", "PC_LOAD"});
            exNote = taken ? "target = pc + offset ; branch TAKEN" : "branch NOT taken, PC keeps sequential value";
            break;
        case Fmt::NONE:
            if (o->op == Opcode::Ret) { ex.insert(ex.end(), {"PC_SRC=R1", "PC_LOAD"}); exNote = "PC <- R1"; }
            else { ex.push_back("(none)"); exNote = "no operation"; }
            break;
    }
    if (o->aluCtrl && o->fmt != Fmt::NONE) ex.push_back(std::string("ALU_CTRL=") + o->aluCtrl);

    std::string wb = dest > 0 ? "REG_WRITE rd=r" + std::to_string(dest) : "(no write-back)";
    std::string wbNote = dest > 0 ? "write result to r" + std::to_string(dest) : "instruction produces no register result";
    std::string dispatch = "DISPATCH(op=" + toBin((uint32_t)o->op, 5) + " -> " + name + " routine)";

    if (!six) {
        m.push_back({0, "IF", "MEM_READ(imem) IR_LOAD PC_INC", "fetch instruction, PC <- PC+1", 1});
        m.push_back({1, "ID", dispatch + " REG_READ IMM_PREP", "decode, read registers, prepare immediate", 2});
        m.push_back({2, "EX/MEM", join(ex), exNote, 3});
        m.push_back({3, "WB", wb, wbNote, 0});
    } else {
        std::string memSig = "(pass-through)", memNote = "no data-memory operation";
        if (o->op == Opcode::Ld) { memSig = "MEM_READ"; memNote = "read data memory"; }
        if (o->op == Opcode::St) { memSig = "MEM_WRITE"; memNote = "write data memory"; }
        m.push_back({0, "IF", "MEM_READ(imem) IR_LOAD PC_INC", "fetch instruction, PC <- PC+1", 1});
        m.push_back({1, "ID", dispatch, "decode instruction", 2});
        m.push_back({2, "RR", "REG_READ IMM_PREP", "read source registers, extend immediate", 3});
        m.push_back({3, "EX", join(ex), exNote, 4});
        m.push_back({4, "MEM", memSig, memNote, 5});
        m.push_back({5, "WB", wb, wbNote, 0});
    }
    return m;
}

// ============================================================================
//  6. PIPELINE TIMING MODEL  (4-stage vs 6-stage)
//
//  The program is executed functionally (correct results); the recorded
//  instruction trace is then replayed through an in-order pipeline model:
//    * one instruction per stage per cycle, stalls back-pressure earlier stages
//    * RAW hazards: with forwarding, a consumer may enter EX one cycle after
//      the producer's result exists (ALU: end of EX; 6-stage load: end of MEM;
//      4-stage load: end of EX/MEM, since memory is inside that stage).
//      without forwarding, the consumer must read the register file at or
//      after the producer's WB cycle (write in first half, read in second).
//    * control hazards: predict not-taken; a taken b/beq/bgt/bsm/call/ret
//      resolves in EX, so the next correct fetch is one cycle after that.
//      penalty = 2 cycles (4-stage) or 3 cycles (6-stage).
// ============================================================================
struct PipeCfg { int stages = 4; bool forward = true; };

struct Sched {
    std::vector<std::array<int, 6>> c;  // c[j][s] = cycle in which instr j enters stage s
    int total = 0;
    int dataStall = 0;
    int ctrlBubbles = 0;
};

static Sched schedule(const std::vector<TraceRec>& tr, PipeCfg cfg) {
    const int S = cfg.stages;
    const int E = (S == 4) ? 2 : 3;  // index of the execute stage
    Sched sc;
    sc.c.assign(tr.size(), std::array<int, 6>{});
    std::array<int, Num_Registers> lastWriter;
    lastWriter.fill(-1);
    for (size_t j = 0; j < tr.size(); ++j) {
        int fetchMin = 1;
        if (j > 0 && tr[j - 1].taken) fetchMin = sc.c[j - 1][E] + 1;  // redirect after branch resolves
        int dataReady = 0;
        for (int r : tr[j].src) {
            int p = lastWriter[r];
            if (p < 0) continue;
            int ready;
            if (cfg.forward) {
                int rs = (tr[p].isLoad && S == 6) ? 4 : E;  // stage whose end produces the value
                ready = sc.c[p][rs] + 1;
            } else ready = sc.c[p][S - 1] + 1;              // after write-back
            dataReady = std::max(dataReady, ready);
        }
        for (int s = 0; s < S; ++s) {
            int t = (s == 0) ? fetchMin : sc.c[j][s - 1] + 1;
            if (j > 0) {
                if (s < S - 1) t = std::max(t, sc.c[j - 1][s + 1]);  // previous must have left this stage
                else t = std::max(t, sc.c[j - 1][s] + 1);
            }
            if (s == E && dataReady > t) { sc.dataStall += dataReady - t; t = dataReady; }
            sc.c[j][s] = t;
        }
        if (tr[j].dest >= 0) lastWriter[tr[j].dest] = (int)j;
        if (tr[j].taken && j + 1 < tr.size()) sc.ctrlBubbles += E;
    }
    sc.total = tr.empty() ? 0 : sc.c.back()[S - 1];
    return sc;
}

static void printDiagram(const Sched& sc, const std::vector<TraceRec>& tr, int S, size_t first, size_t count, int maxCols) {
    static const char* n4[] = {"IF", "ID", "EX", "WB"};
    static const char* n6[] = {"IF", "ID", "RR", "EX", "ME", "WB"};
    const char** nm = (S == 4) ? n4 : n6;
    if (tr.empty() || first >= tr.size()) { printf("  (no instructions executed yet)\n"); return; }
    size_t last = std::min(tr.size(), first + count);
    int t0 = sc.c[first][0], t1 = sc.c[last - 1][S - 1];
    int cols = std::min(maxCols, t1 - t0 + 1);
    printf("  %-30s", "cycle ->");
    for (int t = 0; t < cols; ++t) printf("%-4d", t0 + t);
    printf("\n");
    for (size_t j = first; j < last; ++j) {
        std::string label = fmt("%04u %s", tr[j].pc, tr[j].text.c_str());
        if (label.size() > 28) label = label.substr(0, 28);
        printf("  %-30s", label.c_str());
        for (int t = t0; t < t0 + cols; ++t) {
            std::string cell;
            for (int s = 0; s < S; ++s) {
                int a = sc.c[j][s], b = (s < S - 1) ? sc.c[j][s + 1] : a + 1;
                if (t >= a && t < b) { cell = (t == a) ? ui::cyan(nm[s]) : ui::red("--"); break; }
            }
            int vis = cell.empty() ? 0 : 2;
            printf("%s%*s", cell.c_str(), 4 - vis, "");
        }
        printf("\n");
    }
    printf("  %s\n", ui::dim(S == 4 ? "IF ID EX(=EX/MEM) WB     -- = stalled / held in the stage before" :
                                      "IF ID RR EX ME(=MEM) WB   -- = stalled / held in the stage before").c_str());
}

// ============================================================================
//  7. SHELL : command-line interface
// ============================================================================
static std::string readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { ok = false; return ""; }
    std::stringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

struct Shell {
    CPU cpu;
    bool traceRun = false;   // print every instruction during 'run'
    bool micro = false;      // print microprogram for every executed instruction
    PipeCfg pipe;            // pipeline configuration used by micro / pipe / stats
    std::string loadedPath;

    // -------- loading --------------------------------------------------------
    bool load(const std::string& path) {
        bool ok;
        std::string text = readFile(path, ok);
        if (!ok) { printf("%s cannot open '%s'\n", ui::red("error:").c_str(), path.c_str()); return false; }
        Program p;
        std::vector<AsmError> errs;
        bool binary = looksBinary(text);
        bool good = binary ? loadBinaryText(text, p, errs) : assemble(text, p, errs);
        if (!good) {
            for (auto& e : errs) printf("%s line %d: %s\n", ui::red("error:").c_str(), e.line, e.msg.c_str());
            return false;
        }
        cpu.prog = p;
        cpu.loaded = true;
        cpu.breakpoints.clear();
        cpu.reset();
        loadedPath = path;
        printf("Loaded %s : %zu instructions, %zu labels (%s)\n", path.c_str(), p.words.size(), p.labels.size(),
               binary ? "binary text from assembler.exe" : "assembly source");
        return true;
    }

    bool needLoaded() {
        if (!cpu.loaded) { printf("%s no program loaded. Use: load <file.asm|file.txt>\n", ui::red("error:").c_str()); return false; }
        return true;
    }

    bool parseAddr(const std::string& t, int& out) {
        std::string key = normLabel(t);
        auto it = cpu.prog.labels.find(key);
        if (it != cpu.prog.labels.end()) { out = it->second; return true; }
        long long v;
        if (parseInt(t, v)) { out = (int)v; return true; }
        return false;
    }

    // -------- output helpers -------------------------------------------------
    void printStep(const StepResult& r) {
        std::string changes;
        for (size_t i = 0; i < r.changes.size(); ++i) changes += (i ? ", " : "") + r.changes[i];
        if (r.taken) changes += std::string(changes.empty() ? "" : ", ") + fmt("pc -> %04d", (int)cpu.st.pc);
        printf("  %s %s  %-34s %s\n", ui::dim(fmt("#%-5llu", (unsigned long long)cpu.instrCount)).c_str(),
               ui::bold(fmt("%04u", r.pc)).c_str(), r.text.c_str(), ui::green(changes).c_str());
    }

    void printMicro(const StepResult& r) {
        auto m = microprogram(r.d, pipe.stages, r.taken);
        for (auto& s : m)
            printf("        %s %-6s %-60s %s\n", ui::magenta(fmt("uPC=%02d", s.upc)).c_str(), s.stage.c_str(),
                   s.signals.c_str(), ui::dim("| " + s.note + fmt("  [next=%02d]", s.next)).c_str());
    }

    void printHalt() {
        std::string msg = cpu.exception ? ui::red(cpu.haltReason) : ui::yellow(cpu.haltReason);
        printf("%s %s\n", ui::bold("HALTED:").c_str(), msg.c_str());
        printf("        %llu instructions executed\n", (unsigned long long)cpu.instrCount);
    }

    void printRegs() {
        printf("  %s\n", ui::dim("reg      hex            signed        |  reg      hex            signed").c_str());
        for (int i = 0; i < 16; ++i) {
            std::string cell[2];
            for (int k = 0; k < 2; ++k) {
                int r = i + 16 * k;
                uint32_t v = cpu.st.Reg[r];
                std::string s = fmt("r%02d %-3s 0x%08X %12d", r, regRole(r), v, (int32_t)v);
                if (cpu.changedRegs.count(r)) s = ui::yellow(s);
                else if (v == 0) s = ui::dim(s);
                cell[k] = s;
            }
            printf("  %s   |  %s\n", cell[0].c_str(), cell[1].c_str());
        }
        printf("  pc = %04u    flags: %s    %s\n", cpu.st.pc, flagStr(cpu.st.flags).c_str(),
               ui::dim("(yellow = changed by last instruction)").c_str());
    }

    void printMem(int addr, int n) {
        if (addr < 0 || addr >= Memory_Words) { printf("%s address out of range (0..%d)\n", ui::red("error:").c_str(), Memory_Words - 1); return; }
        for (int a = addr; a < std::min(Memory_Words, addr + n); a += 4) {
            printf("  %s ", ui::cyan(fmt("[%04d]", a)).c_str());
            for (int k = 0; k < 4 && a + k < std::min(Memory_Words, addr + n); ++k) {
                uint32_t v = cpu.st.memory[a + k];
                std::string s = fmt("%08X", v);
                printf(" %s", v ? s.c_str() : ui::dim(s).c_str());
            }
            printf("   ");
            for (int k = 0; k < 4 && a + k < std::min(Memory_Words, addr + n); ++k)
                printf("%d%s", (int32_t)cpu.st.memory[a + k], k < 3 ? ", " : "");
            printf("\n");
        }
    }

    void printList(int start, int n) {
        const auto& P = cpu.prog;
        int end = std::min((int)P.words.size(), start + n);
        printf("  %s\n", ui::dim("    addr  hex       binary fields (op I rd rs1 imm16 | op off27)       instruction").c_str());
        for (int a = std::max(0, start); a < end; ++a) {
            auto lit = P.labelsAt.find(a);
            if (lit != P.labelsAt.end())
                for (auto& nm : lit->second) printf("  %s\n", ui::cyan("." + nm + ":").c_str());
            Instr d = decode(P.words[a]);
            bool isPc = (uint32_t)a == cpu.st.pc && !cpu.halted;
            std::string mark = isPc ? ui::bold("=>") : "  ";
            std::string bp = cpu.breakpoints.count(a) ? ui::red("*") : " ";
            std::string line = fmt("%s%s %04d  %08X  %-38s  %s", mark.c_str(), bp.c_str(), a, P.words[a],
                                   fieldBin(d).c_str(), disasmSym(d, a, P).c_str());
            printf("  %s\n", isPc ? ui::yellow(line).c_str() : line.c_str());
        }
    }

    void printStats() {
        const auto& tr = cpu.trace;
        if (tr.empty()) { printf("  no instructions executed yet\n"); return; }
        size_t ld = 0, stc = 0, ctrl = 0, taken = 0, other = 0;
        for (auto& t : tr) {
            if (t.isLoad) ++ld;
            else if (t.isStore) ++stc;
            else if (t.isCtrl) { ++ctrl; if (t.taken) ++taken; }
            else ++other;
        }
        printf("  instructions executed : %llu%s\n", (unsigned long long)cpu.instrCount,
               tr.size() >= CPU::TRACE_CAP ? "  (pipeline analysis uses first 200000)" : "");
        printf("  mix                   : %zu ALU/other, %zu loads, %zu stores, %zu control (%zu taken)\n", other, ld, stc, ctrl, taken);
        Sched sc = schedule(tr, pipe);
        printf("  pipeline model        : %d-stage, forwarding %s\n", pipe.stages, pipe.forward ? "ON" : "OFF");
        printf("  cycles                : %d   CPI = %.3f\n", sc.total, (double)sc.total / tr.size());
        printf("  data-hazard stalls    : %d cycles\n", sc.dataStall);
        printf("  branch bubbles        : %d cycles (%zu taken transfers x %d)\n", sc.ctrlBubbles, taken, pipe.stages == 4 ? 2 : 3);
    }

    void compare() {
        const auto& tr = cpu.trace;
        if (tr.empty()) { printf("  no instructions executed yet - run the program first\n"); return; }
        printf("  %s\n", ui::bold(fmt("Pipeline comparison for %zu executed instructions", tr.size())).c_str());
        printf("  %-10s %-11s %8s %7s %12s %14s\n", "pipeline", "forwarding", "cycles", "CPI", "data stalls", "branch bubbles");
        int res[2][2] = {};
        for (int s : {4, 6})
            for (int f : {1, 0}) {
                Sched sc = schedule(tr, PipeCfg{s, f == 1});
                res[s == 6][f] = sc.total;
                printf("  %-10s %-11s %8d %7.3f %12d %14d\n", fmt("%d-stage", s).c_str(), f ? "on" : "off", sc.total,
                       (double)sc.total / tr.size(), sc.dataStall, sc.ctrlBubbles);
            }
        double ratio = res[0][1] ? (double)res[1][1] / res[0][1] : 0;
        printf("  %s\n", ui::dim(fmt("With forwarding the 6-stage needs %.0f%% more cycles than the 4-stage, so it only wins if its "
                                      "clock is >= %.2fx faster.", (ratio - 1) * 100, ratio)).c_str());
    }

    // -------- execution ------------------------------------------------------
    void doStep(int n) {
        if (!needLoaded()) return;
        for (int i = 0; i < n; ++i) {
            if (cpu.halted) break;
            StepResult r = cpu.step();
            if (r.executed) { printStep(r); if (micro) printMicro(r); }
        }
        if (cpu.halted) printHalt();
    }

    void doRun(uint64_t maxInstr) {
        if (!needLoaded()) return;
        if (cpu.halted) { printf("  machine is halted (%s). Use 'reset' to start again.\n", cpu.haltReason.c_str()); return; }
        uint64_t n = 0;
        bool first = true;
        while (!cpu.halted && n < maxInstr) {
            if (!first && cpu.breakpoints.count((int)cpu.st.pc)) {
                printf("%s at %04u  (%s)\n", ui::red("Breakpoint hit").c_str(), cpu.st.pc,
                       disasm(decode(cpu.prog.words[cpu.st.pc])).c_str());
                return;
            }
            first = false;
            StepResult r = cpu.step();
            ++n;
            if (r.executed && (traceRun || micro)) { printStep(r); if (micro) printMicro(r); }
        }
        if (cpu.halted) printHalt();
        else printf("  stopped after %llu instructions (limit). Type 'run' to continue.\n", (unsigned long long)n);
    }

    // -------- command dispatcher ----------------------------------------------
    void help() {
        printf("%s\n", ui::bold("Commands").c_str());
        printf("  load <file>            load .asm source or assembler.exe binary-text output\n");
        printf("  asm <src.asm> [out]    assemble and write binary text (one 32-bit word per line)\n");
        printf("  list [addr] [n]  (l)   show program: address, hex, binary fields, instruction\n");
        printf("  step [n]         (s)   execute n instructions (default 1)\n");
        printf("  run [max]        (c)   run until halt / breakpoint (default max 1,000,000)\n");
        printf("  reset                  reset registers, memory, pc (program stays loaded)\n");
        printf("  regs             (r)   show all registers, pc and flags\n");
        printf("  mem [addr] [n]   (m)   dump data memory (n words, default 16)\n");
        printf("  flags                  show Z N V C flags\n");
        printf("  break <addr|label> (b) set breakpoint      break : list breakpoints\n");
        printf("  delete <addr|label|all> remove breakpoint(s)\n");
        printf("  set r<n> <v> | set mem <addr> <v> | set pc <addr>    modify the machine state\n");
        printf("  trace on|off           print each instruction during 'run'\n");
        printf("  micro on|off|4|6       show microprogram (uPC, stage, control signals) per instruction\n");
        printf("  stages 4|6             select pipeline for micro / pipe / stats\n");
        printf("  fwd on|off             enable / disable forwarding in the pipeline model\n");
        printf("  pipe [n]               pipeline diagram of the first n executed instructions (default 12)\n");
        printf("  stats                  instruction mix, cycles and CPI for the selected pipeline\n");
        printf("  compare                4-stage vs 6-stage, with and without forwarding\n");
        printf("  help (h, ?)   quit (q)\n");
    }

    bool onoff(const std::vector<std::string>& t, bool& flag) {
        if (t.size() < 2) { flag = !flag; return true; }
        std::string a = lower(t[1]);
        if (a == "on" || a == "1") flag = true;
        else if (a == "off" || a == "0") flag = false;
        else return false;
        return true;
    }

    bool exec(const std::string& line) {
        std::vector<std::string> t = split(line);
        if (t.empty()) return true;
        std::string c = lower(t[0]);
        long long v;
        if (c == "quit" || c == "q" || c == "exit") return false;
        else if (c == "help" || c == "h" || c == "?") help();
        else if (c == "load") {
            if (t.size() < 2) printf("usage: load <file>\n"); else load(t[1]);
        } else if (c == "asm") {
            if (t.size() < 2) { printf("usage: asm <src.asm> [out.txt]\n"); return true; }
            bool ok;
            std::string text = readFile(t[1], ok);
            if (!ok) { printf("%s cannot open '%s'\n", ui::red("error:").c_str(), t[1].c_str()); return true; }
            Program p;
            std::vector<AsmError> errs;
            if (!assemble(text, p, errs)) {
                for (auto& e : errs) printf("%s line %d: %s\n", ui::red("error:").c_str(), e.line, e.msg.c_str());
                return true;
            }
            std::string outPath = t.size() > 2 ? t[2] : t[1] + ".bin.txt";
            std::ofstream out(outPath);
            for (uint32_t w : p.words) out << toBin(w, 32) << "\n";
            for (size_t a = 0; a < p.words.size(); ++a) {
                Instr d = decode(p.words[a]);
                auto lit = p.labelsAt.find((int)a);
                if (lit != p.labelsAt.end()) for (auto& nm : lit->second) printf("  %s\n", ui::cyan("." + nm + ":").c_str());
                printf("    %04zu  %s  %s\n", a, toBin(p.words[a], 32).c_str(), disasmSym(d, (int)a, p).c_str());
            }
            printf("Wrote %zu words to %s\n", p.words.size(), outPath.c_str());
        } else if (c == "list" || c == "l") {
            if (!needLoaded()) return true;
            int a = (int)cpu.st.pc - 2, n = 14;
            if (a < 0) a = 0;
            if (t.size() > 1) { if (t[1] == "all") { a = 0; n = 100000; } else if (!parseAddr(t[1], a)) { printf("bad address\n"); return true; } }
            if (t.size() > 2 && parseInt(t[2], v)) n = (int)v;
            printList(a, n);
        } else if (c == "step" || c == "s" || c == "n") {
            int n = 1;
            if (t.size() > 1 && parseInt(t[1], v) && v > 0) n = (int)v;
            doStep(n);
        } else if (c == "run" || c == "c" || c == "continue" || c == "go") {
            uint64_t mx = 1000000;
            if (t.size() > 1 && parseInt(t[1], v) && v > 0) mx = (uint64_t)v;
            doRun(mx);
        } else if (c == "reset") {
            if (needLoaded()) { cpu.reset(); printf("  machine reset (pc=0, sp=%d)\n", Memory_Words); }
        } else if (c == "regs" || c == "r" || c == "reg") printRegs();
        else if (c == "flags") printf("  %s\n", flagStr(cpu.st.flags).c_str());
        else if (c == "mem" || c == "m") {
            int a = 0, n = 16;
            if (t.size() > 1 && !parseAddr(t[1], a)) { printf("bad address\n"); return true; }
            if (t.size() > 2 && parseInt(t[2], v)) n = (int)v;
            printMem(a, n);
        } else if (c == "break" || c == "b") {
            if (!needLoaded()) return true;
            if (t.size() < 2) {
                if (cpu.breakpoints.empty()) printf("  no breakpoints\n");
                for (int a : cpu.breakpoints) printf("  * %04d  %s\n", a, a < (int)cpu.prog.words.size() ? disasm(decode(cpu.prog.words[a])).c_str() : "");
                return true;
            }
            int a;
            if (!parseAddr(t[1], a) || a < 0 || a >= (int)cpu.prog.words.size()) { printf("%s bad breakpoint address\n", ui::red("error:").c_str()); return true; }
            cpu.breakpoints.insert(a);
            printf("  breakpoint set at %04d\n", a);
        } else if (c == "delete" || c == "del" || c == "d") {
            if (t.size() < 2) { printf("usage: delete <addr|label|all>\n"); return true; }
            if (lower(t[1]) == "all") { cpu.breakpoints.clear(); printf("  all breakpoints removed\n"); return true; }
            int a;
            if (parseAddr(t[1], a) && cpu.breakpoints.erase(a)) printf("  breakpoint at %04d removed\n", a);
            else printf("  no such breakpoint\n");
        } else if (c == "set") {
            if (t.size() < 3) { printf("usage: set r<n> <v> | set mem <addr> <v> | set pc <addr>\n"); return true; }
            std::string what = lower(t[1]);
            int r = parseReg(what);
            if (r >= 0) {
                if (!parseInt(t[2], v)) { printf("bad value\n"); return true; }
                if (r == 0) printf("  r0 is the ground register and always holds 0\n");
                else { cpu.st.Reg[r] = (uint32_t)v; printf("  r%d = %d (0x%X)\n", r, (int32_t)v, (uint32_t)v); }
            } else if (what == "mem" && t.size() >= 4) {
                int a;
                long long val;
                if (!parseAddr(t[2], a) || !parseInt(t[3], val) || a < 0 || a >= Memory_Words) { printf("bad address/value\n"); return true; }
                cpu.st.memory[a] = (uint32_t)val;
                printf("  mem[%d] = %d (0x%X)\n", a, (int32_t)val, (uint32_t)val);
            } else if (what == "pc") {
                int a;
                if (!parseAddr(t[2], a) || a < 0) { printf("bad address\n"); return true; }
                cpu.st.pc = (uint32_t)a;
                cpu.halted = false;
                printf("  pc = %04d\n", a);
            } else printf("usage: set r<n> <v> | set mem <addr> <v> | set pc <addr>\n");
        } else if (c == "trace") {
            if (!onoff(t, traceRun)) printf("usage: trace on|off\n"); else printf("  trace %s\n", traceRun ? "on" : "off");
        } else if (c == "micro") {
            if (t.size() > 1 && (t[1] == "4" || t[1] == "6")) { pipe.stages = t[1] == "4" ? 4 : 6; micro = true; }
            else if (!onoff(t, micro)) { printf("usage: micro on|off|4|6\n"); return true; }
            printf("  microprogram view %s (%d-stage control)\n", micro ? "on" : "off", pipe.stages);
        } else if (c == "stages") {
            if (t.size() > 1 && (t[1] == "4" || t[1] == "6")) { pipe.stages = t[1] == "4" ? 4 : 6; printf("  pipeline model: %d-stage\n", pipe.stages); }
            else printf("usage: stages 4|6\n");
        } else if (c == "fwd") {
            if (!onoff(t, pipe.forward)) printf("usage: fwd on|off\n"); else printf("  forwarding %s\n", pipe.forward ? "on" : "off");
        } else if (c == "pipe") {
            size_t n = 12;
            if (t.size() > 1 && parseInt(t[1], v) && v > 0) n = (size_t)v;
            Sched sc = schedule(cpu.trace, pipe);
            printf("  %s\n", ui::bold(fmt("%d-stage pipeline, forwarding %s", pipe.stages, pipe.forward ? "on" : "off")).c_str());
            printDiagram(sc, cpu.trace, pipe.stages, 0, n, 34);
        } else if (c == "stats") printStats();
        else if (c == "compare") compare();
        else printf("  unknown command '%s' - type 'help'\n", c.c_str());
        return true;
    }
};

static void banner() {
    printf("%s\n", ui::bold(ui::cyan("==============================================================")).c_str());
    printf("%s\n", ui::bold(ui::cyan("  RISC201 simulator   32-bit RISC | 32 regs | 25 instructions")).c_str());
    printf("%s\n", ui::bold(ui::cyan("==============================================================")).c_str());
    printf("  type 'help' for commands, 'load <file>' to begin\n\n");
}

int main(int argc, char** argv) {
    std::string file;
    bool runNow = false, noColor = false, quiet = false, doCompare = false, doPipe = false;
    bool asmMode = false, listMode = false;
    Shell sh;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--run" || a == "-r") runNow = true;
        else if (a == "--asm" || a == "-a") asmMode = true;
        else if (a == "--list" || a == "-l") listMode = true;
        else if (a == "--trace" || a == "-t") sh.traceRun = true;
        else if (a == "--micro" || a == "-m") sh.micro = true;
        else if (a == "--no-color") noColor = true;
        else if (a == "--no-forward") sh.pipe.forward = false;
        else if (a == "--stages" && i + 1 < argc) sh.pipe.stages = (std::string(argv[++i]) == "6") ? 6 : 4;
        else if (a == "--compare") doCompare = true;
        else if (a == "--pipe") doPipe = true;
        else if (a == "-q") quiet = true;
        else if (a == "--help" || a == "-h") {
            printf("usage: risc201_sim [file.asm|file.txt] [options]\n"
                   "  -a, --asm        assemble file and print 32-bit binary to terminal\n"
                   "  -l, --list       display binary fields and instructions breakdown\n"
                   "  -r, --run        run the program to completion, print registers + stats, exit\n"
                   "  -t, --trace      print each executed instruction\n"
                   "  -m, --micro      show the microprogram (uPC / control signals) for each instruction\n"
                   "  --stages 4|6     pipeline / control unit to model (default 4)\n"
                   "  --no-forward     disable forwarding in the pipeline model\n"
                   "  --pipe           print a pipeline diagram after the run\n"
                   "  --compare        print 4-stage vs 6-stage comparison after the run\n"
                   "  --no-color  -q   plain output / no banner\n");
            return 0;
        } else file = a;
    }
#ifdef _WIN32
    {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD m = 0;
        ui::color = !noColor && SIM_ISATTY(stdout) && GetConsoleMode(h, &m) && SetConsoleMode(h, m | 0x0004);
    }
#else
    ui::color = !noColor && SIM_ISATTY(stdout);
#endif
    if (!quiet && !asmMode && !listMode) banner();

    if (asmMode) {
        if (file.empty()) {
            printf("%s --asm requires a file. Example: ./simulator/simulator -a <file.asm>\n", ui::red("error:").c_str());
            return 1;
        }
        sh.exec("asm " + file);
        return 0;
    }

    if (!file.empty() && !sh.load(file)) return 1;

    if (listMode) {
        sh.exec("list all");
        return 0;
    }

    if (runNow) {
        sh.doRun(1000000);
        printf("\n");
        sh.printRegs();
        printf("\n");
        sh.printStats();
        if (doPipe) { printf("\n"); sh.exec("pipe 16"); }
        if (doCompare) { printf("\n"); sh.compare(); }
        return sh.cpu.exception ? 2 : 0;
    }
    std::string line;
    while (true) {
        printf("%s", ui::bold("risc201> ").c_str());
        fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        if (!SIM_ISATTY(stdin)) printf("%s\n", line.c_str());  // echo when scripted
        if (!sh.exec(line)) break;
    }
    return 0;
}
