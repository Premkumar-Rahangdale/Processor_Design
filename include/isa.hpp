#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <string>

using namespace std;

namespace isa{
    unordered_map<string, string> opcode = {
        {"add", "00001"}, {"sub", "00010"}, {"mul", "00011"}, {"div", "00100"},
        {"mod", "00101"}, {"cmp", "00110"}, {"and", "00111"}, {"or", "01000"},
        {"not", "01001"}, {"xor", "01010"}, {"mov", "01011"}, {"movu", "01100"}, 
        {"movh", "01101"}, {"lsl", "01110"}, {"lsr", "01111"}, {"asr", "10000"}, 
        {"nop", "10001"}, {"ld", "10010"}, {"st", "10011"}, {"beq", "10100"}, 
        {"bgt", "10101"}, {"bsm", "10110"}, {"b", "10111"}, {"call", "11000"},
        {"ret", "11001"}
    };

    unordered_map<string, int> branch = {
        {"add", 3}, {"sub", 3}, {"mul", 3}, {"div", 3}, {"mod", 3},
        {"cmp", 2}, {"and", 3}, {"or", 3}, {"not", 2}, {"xor", 3},
        {"mov", 2}, {"movu", 2}, {"movh", 2}, {"lsl", 3}, {"lsr", 3},
        {"asr", 3}, {"nop", 0}, {"ld", 3}, {"st", 3}, {"beq", 1},
        {"bgt", 1}, {"bsm", 1}, {"b", 1}, {"call", 1}, {"ret", 0}
    };
};
// constexpr unsigned OPCODE_BITS  = 5;
// constexpr unsigned OPCODE_SHIFT = 32 - OPCODE_BITS;
// constexpr uint32_t OPCODE_MASK  = (1u << OPCODE_BITS) - 1;

// enum class Opcode : uint8_t{
//     // Arithmetic
//     Add = 0b00001, Sub = 0b00010, Mul = 0b00011, Div = 0b00100, Mod = 0b00101,
//     Cmp = 0b00110, And = 0b00111, Or  = 0b01000, Not = 0b01001, Xor = 0b01010,
//     // memory transfer and shift
//     Mov = 0b01011, Movu = 0b01100, Movh = 0b01101,
//     Lsl = 0b01110, Lsr = 0b01111, Asr = 0b10000,
//     Nop = 0b10001, Ld = 0b10010, St = 0b10011,
//     // branch
//     Beq = 0b10100, Bgt = 0b10101, Bsm = 0b10110, B = 0b10111,
//     Call = 0b11000, Ret = 0b11001
// };

// constexpr uint32_t opcodeBits(Opcode op) {}

// constexpr bool isValidOpcode(uint32_t raw5) {}

// constexpr Opcode decodeOpcode(uint32_t word) {}

// constexpr std::string_view mnemonic(Opcode opcode) {}