#include <iostream>
#include <map>
#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>
using namespace std;

uint32_t extractOpcode(uint32_t instruction) {
    return (instruction >> 27) & 0x1F;
}

uint32_t extractImmediate(uint32_t instruction) {
    return (instruction >> 26) & 1;
}

uint32_t extractRd(uint32_t instruction) {
    return (instruction >> 21) & 0x1F;
}

uint32_t extractRS1(uint32_t instruction) {
    return (instruction >> 16) & 0x1F;
}

uint32_t extractRS2(uint32_t instruction) {
    return (instruction >> 11) & 0x1F;
}

int32_t signExtend(uint32_t v, int bits) {
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

int32_t extractImm(uint32_t x) { 
    return signExtend(x & 0xFFFF, 16); 
}

int32_t extractOffset(uint32_t x) { 
    return signExtend(x & 0x7FFFFFF, 27); 
}

map<int, string> Opcode {
    {1, "add"},
    {2, "sub"},
    {3, "mul"},
    {4, "div"},
    {5, "mod"},
    {6, "cmp"},
    {7, "and"},
    {8, "or"},
    {9, "not"},
    {10, "xor"},
    {11, "mov"},
    {12, "movu"},
    {13, "movh"},
    {14, "lsl"},
    {15, "lsr"},
    {16, "asr"},
    {17, "nop"},
    {18, "ld"},
    {19, "st"},
    {20, "beq"},
    {21, "bgt"},
    {22, "bsm"},
    {23, "b"},
    {24, "call"},
    {25, "ret"},
};


string disassemble(uint32_t instruction) {
    uint32_t opcode = extractOpcode(instruction);
    int i = extractImmediate(instruction);

    string rd = to_string(extractRd(instruction));
    string rs1 = to_string(extractRS1(instruction));
    string rs2;
    string imm;

    if (i)
        imm = to_string(extractImm(instruction));
    else
        rs2 = to_string(extractRS2(instruction));

    auto it = Opcode.find(opcode);

    if (it == Opcode.end())
        return "invalid opcode";

    string op = it->second;

    if (op == "nop" || op == "ret") {
        return op;
    } else if (op == "b" || op == "call" || op == "beq" || op == "bgt" || op == "bsm"){
        return op + " " + to_string(extractOffset(instruction));
    } else if (op == "ld" || op == "st") {
        return op + " r" + rd + " " + to_string(extractImm(instruction)) + "[r" + rs1 + "]";
    } else if (op == "not" || op == "mov" || op == "movu" || op == "movh"){
        if (i)
            return op + " r" + rd + " " + imm;
        else
            return op + " r" + rd + " r" + rs2;
    } else if (op == "cmp") {
        if (i)
            return op + " r" + rs1 + " " + imm;
        else
            return op + " r" + rs1 + " r" + rs2;
    } else{
        if (i)
            return op + " r" + rd + " r" + rs1 + " " + imm;
        else
            return op + " r" + rd + " r" + rs1 + " r" + rs2;
    }
}


int main() {
    ifstream in("tests/o1_asmblr.txt");
    ofstream out("tests/o1_disasmblr.txt");

    string ins;

    while (getline(in, ins)){
        stringstream ss(ins);
        string token;
        ss >> token;

        if (token.empty()) {
            out << "\n";
            continue;
        }
        if (token[0] == '.'){
            out << token << "\n";
        } else {
            uint32_t instruction = (uint32_t)stoul(token, nullptr, 2);

            string output = disassemble(instruction);

            out << output << "\n";
        }

        out.flush();
    }
    return 0;
}
