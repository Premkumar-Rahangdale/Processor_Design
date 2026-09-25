#include "isa.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace risc201 {

static const std::unordered_map<std::string, InstructionInfo> instructionTable = {
    {"ADD",  {"ADD",  0x01, Format::R}},
    {"SUB",  {"SUB",  0x02, Format::R}},
    {"AND",  {"AND",  0x03, Format::R}},
    {"OR",   {"OR",   0x04, Format::R}},
    {"XOR",  {"XOR",  0x05, Format::R}},
    {"SLL",  {"SLL",  0x06, Format::R}},
    {"SRL",  {"SRL",  0x07, Format::R}},
    {"SRA",  {"SRA",  0x08, Format::R}},
    {"SLT",  {"SLT",  0x09, Format::R}},
    {"SLTU", {"SLTU", 0x0A, Format::R}},

    {"ADDI", {"ADDI", 0x0B, Format::I}},
    {"ANDI", {"ANDI", 0x0C, Format::I}},
    {"ORI",  {"ORI",  0x0D, Format::I}},
    {"XORI", {"XORI", 0x0E, Format::I}},
    {"SLLI", {"SLLI", 0x0F, Format::I}},
    {"SRLI", {"SRLI", 0x10, Format::I}},

    {"LW",   {"LW",   0x15, Format::I}},
    {"SW",   {"SW",   0x16, Format::S}},

    {"BEQ",  {"BEQ",  0x18, Format::B}},
    {"JAL",  {"JAL",  0x1F, Format::J}}
};

const InstructionInfo* getInstruction(const std::string& mnemonic)
{
    std::string key = mnemonic;

    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        }
    );

    auto it = instructionTable.find(key);

    if (it == instructionTable.end())
        return nullptr;

    return &it->second;
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

    auto alias = aliases.find(r);

    if (alias != aliases.end())
        return alias->second;


//dobara dekhle jiski gand mei dum hai
    if (r.size() >= 2 && r[0] == 'r') {
        int value = 0;

        for (size_t i = 1; i < r.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(r[i])))
                throw std::runtime_error("Invalid register: " + token);

            value = value * 10 + (r[i] - '0');
        }

        if (value >= 0 && value <= 31)
            return value;
    }

    throw std::runtime_error("Invalid register: " + token);
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