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

    unordered_map<int, string> inverseOpcode {
    {1, "add"}, {2, "sub"}, {3, "mul"}, {4, "div"}, {5, "mod"},
    {6, "cmp"}, {7, "and"}, {8, "or"}, {9, "not"}, {10, "xor"},
    {11, "mov"}, {12, "movu"}, {13, "movh"}, {14, "lsl"}, {15, "lsr"},
    {16, "asr"}, {17, "nop"}, {18, "ld"}, {19, "st"}, {20, "beq"},
    {21, "bgt"}, {22, "bsm"}, {23, "b"}, {24, "call"}, {25, "ret"}
    };
};