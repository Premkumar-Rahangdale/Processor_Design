#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "microinstr.hpp"

// ===========================================================================
//  The RISC201 microprogram (symbolic) and the micro-assembler that resolves
//  its labels. Both control units load the same MicroProgram; they only differ
//  in how they encode and fetch it.
// ===========================================================================

struct MicroProgram {
    std::vector<MicroInstr> words;          // control-memory image: address = index
    std::string beginLabel = ".begin";      // reset micro-pc (start of the preamble)
    std::string exceptionLabel = ".exception";   // entered when a unit raises an exception
    std::string opcodeLabel[32];            // switch unit: opcode -> routine label ("" = illegal opcode)
};

// Microcode for the 25 RISC201 instructions plus the preamble and the
// exception routine.
MicroProgram buildRisc201Microprogram();

// Result of the micro-assembler's second pass
struct AssembledMicroProgram {
    std::vector<MicroInstr> words;
    std::vector<uint16_t> targetAddr;       // resolved branch target of every word (0 if unused)
    uint16_t beginAddr = 0;
    uint16_t exceptionAddr = 0;
    int16_t switchAddr[32];                 // opcode -> micro-address, -1 = illegal opcode
};

// Pass 1: label -> address.  Pass 2: resolve targets and the switch table.
// Throws std::runtime_error on a missing or duplicate label, or if the program
// does not fit in the control memory.
AssembledMicroProgram assembleMicroProgram(const MicroProgram& prog);
