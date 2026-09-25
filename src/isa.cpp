#include "isa.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace risc201 {

static const InstructionInfo instructionTable[] = {

    // R-Type
    {"ADD",   0x01, Format::R},
    {"SUB",   0x02, Format::R},
    {"AND",   0x03, Format::R},
    {"OR",    0x04, Format::R},
    {"XOR",   0x05, Format::R},
    {"SLL",   0x06, Format::R},
    {"SRL",   0x07, Format::R},
    {"SRA",   0x08, Format::R},
    {"SLT",   0x09, Format::R},
    {"SLTU",  0x0A, Format::R},

    // I-Type
    {"ADDI",  0x0B, Format::I},
    {"ANDI",  0x0C, Format::I},
    {"ORI",   0x0D, Format::I},
    {"XORI",  0x0E, Format::I},
    {"SLLI",  0x0F, Format::I},
    {"SRLI",  0x10, Format::I},
    {"SRAI",  0x11, Format::I},
    {"SLTI",  0x12, Format::I},
    {"LUI",   0x13, Format::I},
    {"LW",    0x14, Format::I},
    {"LB",    0x15, Format::I},
    {"LBU",   0x16, Format::I},

    // S-Type
    {"SW",    0x17, Format::S},
    {"SB",    0x18, Format::S},

    // B-Type
    {"BEQ",   0x19, Format::B},
    {"BNE",   0x1A, Format::B},
    {"BLT",   0x1B, Format::B},
    {"BGE",   0x1C, Format::B},

    // J-Type
    {"JAL",   0x1D, Format::J},

    // I-Type
    {"JALR",  0x1E, Format::I},
    {"ECALL", 0x1F, Format::I},

    // R-Type
    {"HALT",  0x20, Format::R}
};

static constexpr size_t instructionCount =
    sizeof(instructionTable) / sizeof(instructionTable[0]);

static std::string toUpper(std::string s)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        }
    );

    return s;
}

const InstructionInfo* getInstruction(const std::string& mnemonic)
{
    std::string key = toUpper(mnemonic);

    for (size_t i = 0; i < instructionCount; ++i) {
        if (key == instructionTable[i].mnemonic) {
            return &instructionTable[i];
        }
    }

    return nullptr;
}

bool isRegister(const std::string& token)
{
    try {
        parseRegister(token);
        return true;
    }
    catch (...) {
        return false;
    }
}

int parseRegister(const std::string& token)
{
    static const std::unordered_map<std::string, int> aliases = {

        {"zero", 0},
        {"ra",   1},
        {"sp",   2},
        {"gp",   3},

        {"a0",   4},
        {"a1",   5},
        {"a2",   6},
        {"a3",   7},

        {"t0",   8},
        {"t1",   9},
        {"t2",  10},
        {"t3",  11},
        {"t4",  12},
        {"t5",  13},
        {"t6",  14},
        {"t7",  15},

        {"s0",  16},
        {"s1",  17},
        {"s2",  18},
        {"s3",  19},
        {"s4",  20},
        {"s5",  21},
        {"s6",  22},
        {"s7",  23},

        {"t8",  24},
        {"t9",  25},
        {"t10", 26},
        {"t11", 27},

        {"fp",  28},
        {"k0",  29},
        {"k1",  30},
        {"lr",  31}
    };

    std::string r = token;

    std::transform(
        r.begin(),
        r.end(),
        r.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    auto it = aliases.find(r);

    if (it != aliases.end()) {
        return it->second;
    }

    if (r.size() >= 2 && r[0] == 'r') {

        int value = 0;

        for (size_t i = 1; i < r.size(); ++i) {

            if (!std::isdigit(
                    static_cast<unsigned char>(r[i]))) {
                throw std::runtime_error(
                    "Invalid register: " + token
                );
            }

            value = value * 10 + (r[i] - '0');
        }

        if (value >= 0 && value <= 31) {
            return value;
        }
    }

    throw std::runtime_error(
        "Invalid register: " + token
    );
}
bool isImmediateInstruction(const std::string& mnemonic)
{
    static const std::unordered_set<std::string> instructions = {
        "ADDI",
        "ANDI",
        "ORI",
        "XORI",
        "SLLI",
        "SRLI",
        "SRAI",
        "SLTI",
        "LUI"
    };

    return instructions.count(toUpper(mnemonic)) > 0;
}

bool isMemoryInstruction(const std::string& mnemonic)
{
    static const std::unordered_set<std::string> instructions = {
        "LW",
        "LB",
        "LBU",
        "SW",
        "SB"
    };

    return instructions.count(toUpper(mnemonic)) > 0;
}

bool isBranchInstruction(const std::string& mnemonic)
{
    static const std::unordered_set<std::string> instructions = {
        "BEQ",
        "BNE",
        "BLT",
        "BGE"
    };

    return instructions.count(toUpper(mnemonic)) > 0;
}

bool isJumpInstruction(const std::string& mnemonic)
{
    static const std::unordered_set<std::string> instructions = {
        "JAL",
        "JALR"
    };

    return instructions.count(toUpper(mnemonic)) > 0;
}
} // namespace risc201