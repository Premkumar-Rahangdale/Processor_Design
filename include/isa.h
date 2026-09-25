#ifndef RISC201_ISA_H
#define RISC201_ISA_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace risc201 {

enum class Format {
    R,
    I,
    S,
    B,
    J
};

struct InstructionInfo {
    std::string mnemonic;
    uint8_t opcode;
    Format format;
};

const InstructionInfo* getInstruction(const std::string& mnemonic);

bool isRegister(const std::string& token);
int parseRegister(const std::string& token);

bool isImmediateInstruction(const std::string& mnemonic);
bool isMemoryInstruction(const std::string& mnemonic);
bool isBranchInstruction(const std::string& mnemonic);
bool isJumpInstruction(const std::string& mnemonic);

} // namespace risc201

#endif