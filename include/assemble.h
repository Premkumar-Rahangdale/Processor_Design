#ifndef RISC201_ASSEMBLER_H
#define RISC201_ASSEMBLER_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace risc201 {

class Assembler {
public:
    // Assemble a complete RISC201 assembly program
    std::vector<uint32_t> assemble(
        const std::vector<std::string>& source
    );

    // Assemble a source file and write machine code to output file
    void assembleFile(
        const std::string& inputFile,
        const std::string& outputFile
    );

private:
    // Symbol table:
    // label -> byte address
    std::unordered_map<std::string, uint32_t> symbolTable;

    // Preprocess the source.
    // Handles comments, labels, and synthetic instructions.
    std::vector<std::string> preprocess(
        const std::vector<std::string>& source
    );

    // First pass:
    // Determine addresses of labels and build symbol table.
    void firstPass(
        const std::vector<std::string>& lines
    );

    // Second pass:
    // Convert one instruction into its 32-bit machine-code word.
    uint32_t secondPassInstruction(
        const std::string& line,
        uint32_t pc
    );

    // String processing
    static std::string trim(
        const std::string& str
    );

    static std::vector<std::string> tokenize(
        const std::string& line
    );

    // Immediate and number parsing
    static int32_t parseImmediate(
        const std::string& token
    );

    static int32_t parseNumber(
        const std::string& token
    );

    // Range checking
    static bool fitsSigned(
        int64_t value,
        int bits
    );

    static bool fitsUnsigned(
        int64_t value,
        int bits
    );
};

} // namespace risc201

#endif