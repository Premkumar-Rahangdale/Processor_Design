#include "assembler.h"
#include "isa.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace risc201 {

// ------------------------------------------------------------
// Utility functions
// ------------------------------------------------------------

std::string Assembler::trim(const std::string& str)
{
    size_t start = 0;

    while (start < str.size() &&
           std::isspace(static_cast<unsigned char>(str[start])))
    {
        ++start;
    }

    size_t end = str.size();

    while (end > start &&
           std::isspace(static_cast<unsigned char>(str[end - 1])))
    {
        --end;
    }

    return str.substr(start, end - start);
}


std::vector<std::string> Assembler::tokenize(const std::string& line)
{
    std::string cleaned = line;

    // Remove comments.
    size_t comment = cleaned.find('#');

    if (comment != std::string::npos)
        cleaned = cleaned.substr(0, comment);

    // Replace commas and parentheses with spaces.
    for (char& c : cleaned)
    {
        if (c == ',' || c == '(' || c == ')')
            c = ' ';
    }

    std::stringstream ss(cleaned);

    std::vector<std::string> tokens;
    std::string token;

    while (ss >> token)
        tokens.push_back(token);

    return tokens;
}


// ------------------------------------------------------------
// Number parsing
// ------------------------------------------------------------

int32_t Assembler::parseNumber(const std::string& token)
{
    std::string s = token;

    if (s.empty())
        throw std::runtime_error("Empty number");

    size_t index = 0;
    bool negative = false;

    if (s[index] == '+' || s[index] == '-')
    {
        negative = (s[index] == '-');
        ++index;
    }

    int base = 10;

    if (index + 1 < s.size() &&
        s[index] == '0' &&
        (s[index + 1] == 'x' || s[index + 1] == 'X'))
    {
        base = 16;
        index += 2;
    }
    else if (index + 1 < s.size() &&
             s[index] == '0' &&
             (s[index + 1] == 'b' || s[index + 1] == 'B'))
    {
        base = 2;
        index += 2;
    }

    if (index >= s.size())
        throw std::runtime_error("Invalid number: " + token);

    int64_t value = 0;

    for (; index < s.size(); ++index)
    {
        char c = s[index];

        int digit = -1;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            throw std::runtime_error("Invalid number: " + token);

        if (digit >= base)
            throw std::runtime_error("Invalid number: " + token);

        value = value * base + digit;
    }

    if (negative)
        value = -value;

    if (value < INT32_MIN || value > INT32_MAX)
        throw std::runtime_error("Number outside 32-bit range: " + token);

    return static_cast<int32_t>(value);
}


int32_t Assembler::parseImmediate(const std::string& token)
{
    return parseNumber(token);
}


// ------------------------------------------------------------
// Range checking
// ------------------------------------------------------------

bool Assembler::fitsSigned(int64_t value, int bits)
{
    const int64_t minValue = -(1LL << (bits - 1));
    const int64_t maxValue =  (1LL << (bits - 1)) - 1;

    return value >= minValue && value <= maxValue;
}


bool Assembler::fitsUnsigned(int64_t value, int bits)
{
    const int64_t maxValue = (1LL << bits) - 1;

    return value >= 0 && value <= maxValue;
}


// ------------------------------------------------------------
// Preprocessor
// ------------------------------------------------------------

std::vector<std::string> Assembler::preprocess(
    const std::vector<std::string>& source)
{
    std::vector<std::string> result;

    for (const std::string& originalLine : source)
    {
        std::string line = trim(originalLine);

        if (line.empty())
            continue;

        // Remove comments.
        size_t comment = line.find('#');

        if (comment != std::string::npos)
            line = trim(line.substr(0, comment));

        if (line.empty())
            continue;

        // ----------------------------------------------------
        // Preserve labels.
        // ----------------------------------------------------

        size_t colon = line.find(':');

        std::string labelPart;
        std::string instructionPart;

        if (colon != std::string::npos)
        {
            labelPart = trim(line.substr(0, colon));
            instructionPart = trim(line.substr(colon + 1));

            result.push_back(labelPart + ":");

            if (instructionPart.empty())
                continue;

            line = instructionPart;
        }

        std::vector<std::string> tokens = tokenize(line);

        if (tokens.empty())
            continue;

        std::string mnemonic = tokens[0];

        std::transform(
            mnemonic.begin(),
            mnemonic.end(),
            mnemonic.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(std::toupper(c));
            }
        );

        // ----------------------------------------------------
        // NOP
        //
        // Q1:
        // NOP -> ADDI R0, R0, 0
        // ----------------------------------------------------

        if (mnemonic == "NOP")
        {
            result.push_back("ADDI R0, R0, 0");
            continue;
        }

        // ----------------------------------------------------
        // MOV
        //
        // Q1:
        // MOV Rd, Rs -> ADD Rd, Rs, R0
        // ----------------------------------------------------

        if (mnemonic == "MOV")
        {
            if (tokens.size() != 3)
                throw std::runtime_error(
                    "MOV requires: MOV Rd, Rs"
                );

            result.push_back(
                "ADD " + tokens[1] + ", " +
                tokens[2] + ", R0"
            );

            continue;
        }

        // ----------------------------------------------------
        // NEG
        //
        // Q1:
        // NEG Rd, Rs -> SUB Rd, R0, Rs
        // ----------------------------------------------------

        if (mnemonic == "NEG")
        {
            if (tokens.size() != 3)
                throw std::runtime_error(
                    "NEG requires: NEG Rd, Rs"
                );

            result.push_back(
                "SUB " + tokens[1] + ", R0, " +
                tokens[2]
            );

            continue;
        }

        // ----------------------------------------------------
        // J
        //
        // Q1:
        // J label -> JAL R0, label
        // ----------------------------------------------------

        if (mnemonic == "J")
        {
            if (tokens.size() != 2)
                throw std::runtime_error(
                    "J requires: J label"
                );

            result.push_back(
                "JAL R0, " + tokens[1]
            );

            continue;
        }

        // ----------------------------------------------------
        // PUSH
        //
        // Full-descending stack.
        //
        // Expansion:
        // ADDI SP, SP, -4
        // SW Rs, 0(SP)
        // ----------------------------------------------------

        if (mnemonic == "PUSH")
        {
            if (tokens.size() != 2)
                throw std::runtime_error(
                    "PUSH requires: PUSH Rs"
                );

            result.push_back("ADDI R2, R2, -4");

            result.push_back(
                "SW " + tokens[1] + ", 0(R2)"
            );

            continue;
        }

        // ----------------------------------------------------
        // POP
        //
        // Expansion:
        // LW Rd, 0(SP)
        // ADDI SP, SP, 4
        // ----------------------------------------------------

        if (mnemonic == "POP")
        {
            if (tokens.size() != 2)
                throw std::runtime_error(
                    "POP requires: POP Rd"
                );

            result.push_back(
                "LW " + tokens[1] + ", 0(R2)"
            );

            result.push_back("ADDI R2, R2, 4");

            continue;
        }

        // Normal instruction.
        result.push_back(line);
    }

    return result;
}


// ------------------------------------------------------------
// First pass
// ------------------------------------------------------------

void Assembler::firstPass(
    const std::vector<std::string>& lines)
{
    symbolTable.clear();

    uint32_t pc = 0;

    for (const std::string& line : lines)
    {
        std::string current = trim(line);

        if (current.empty())
            continue;

        // Label.
        size_t colon = current.find(':');

        if (colon != std::string::npos)
        {
            std::string label =
                trim(current.substr(0, colon));

            if (label.empty())
                throw std::runtime_error(
                    "Empty label"
                );

            if (symbolTable.find(label) !=
                symbolTable.end())
            {
                throw std::runtime_error(
                    "Duplicate label: " + label
                );
            }

            symbolTable[label] = pc;

            current =
                trim(current.substr(colon + 1));

            if (current.empty())
                continue;
        }

        // Every RISC201 instruction is 32 bits
        // and therefore occupies 4 bytes.
        pc += 4;
    }
}


// ------------------------------------------------------------
// Second pass
// ------------------------------------------------------------

uint32_t Assembler::secondPassInstruction(
    const std::string& line,
    uint32_t pc)
{
    std::vector<std::string> tokens = tokenize(line);

    if (tokens.empty())
        throw std::runtime_error(
            "Empty instruction"
        );

    std::string mnemonic = tokens[0];

    std::transform(
        mnemonic.begin(),
        mnemonic.end(),
        mnemonic.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::toupper(c)
            );
        }
    );

    const InstructionInfo* info =
        getInstruction(mnemonic);

    if (info == nullptr)
    {
        throw std::runtime_error(
            "Unknown instruction: " + mnemonic
        );
    }

    uint32_t opcode = info->opcode;

    // --------------------------------------------------------
    // R-Type
    //
    // Opcode[5]
    // rd[5]
    // rs1[5]
    // rs2[5]
    // funct[5]
    // unused[6]
    // --------------------------------------------------------

    if (info->format == Format::R)
    {
        if (tokens.size() < 4)
            throw std::runtime_error(
                mnemonic + " requires 3 registers"
            );

        int rd  = parseRegister(tokens[1]);
        int rs1 = parseRegister(tokens[2]);
        int rs2 = parseRegister(tokens[3]);

        if (rd < 0 || rs1 < 0 || rs2 < 0)
            throw std::runtime_error(
                "Invalid register in " + mnemonic
            );

        uint32_t instruction = 0;

        instruction |= (opcode & 0x3F) << 26;
        instruction |= (rd & 0x1F) << 21;
        instruction |= (rs1 & 0x1F) << 16;
        instruction |= (rs2 & 0x1F) << 11;

        // Q1 does not assign separate funct values
        // in the instruction catalog.
        instruction |= 0 << 6;

        return instruction;
    }

    // --------------------------------------------------------
    // I-Type
    //
    // Opcode[6]
    // rd[5]
    // rs1[5]
    // immediate[16]
    // --------------------------------------------------------

    if (info->format == Format::I)
    {
        if (tokens.size() < 3)
            throw std::runtime_error(
                mnemonic + " has insufficient operands"
            );

        int rd = parseRegister(tokens[1]);

        if (rd < 0)
            throw std::runtime_error(
                "Invalid destination register in " +
                mnemonic
            );

        int rs1 = 0;
        int32_t immediate = 0;

        // LUI has no normal rs1 operand.
        if (mnemonic == "LUI")
        {
            immediate = parseImmediate(tokens[2]);

            if (!fitsSigned(immediate, 16) &&
                !fitsUnsigned(immediate, 16))
            {
                throw std::runtime_error(
                    "LUI immediate does not fit 16 bits"
                );
            }
        }
        else
        {
            rs1 = parseRegister(tokens[2]);

            if (rs1 < 0)
                throw std::runtime_error(
                    "Invalid source register in " +
                    mnemonic
                );

            if (tokens.size() < 4)
                throw std::runtime_error(
                    mnemonic + " requires an immediate"
                );

            immediate =
                parseImmediate(tokens[3]);
        }

        if (!fitsSigned(immediate, 16) &&
            !fitsUnsigned(immediate, 16))
        {
            throw std::runtime_error(
                "Immediate does not fit 16 bits in " +
                mnemonic
            );
        }

        uint32_t instruction = 0;

        instruction |= (opcode & 0x3F) << 26;
        instruction |= (rd & 0x1F) << 21;
        instruction |= (rs1 & 0x1F) << 16;
        instruction |=
            static_cast<uint16_t>(immediate);

        return instruction;
    }

    // --------------------------------------------------------
    // S-Type
    //
    // Opcode[6]
    // rs2[5]
    // rs1[5]
    // immediate[16]
    // --------------------------------------------------------

    if (info->format == Format::S)
    {
        if (tokens.size() < 4)
            throw std::runtime_error(
                mnemonic + " requires Rs2, offset(Rs1)"
            );

        int rs2 = parseRegister(tokens[1]);
        int32_t immediate =
            parseImmediate(tokens[2]);
        int rs1 = parseRegister(tokens[3]);

        if (rs1 < 0 || rs2 < 0)
            throw std::runtime_error(
                "Invalid register in " + mnemonic
            );

        if (!fitsSigned(immediate, 16))
            throw std::runtime_error(
                "Offset does not fit signed 16 bits"
            );

        uint32_t instruction = 0;

        instruction |= (opcode & 0x3F) << 26;
        instruction |= (rs2 & 0x1F) << 21;
        instruction |= (rs1 & 0x1F) << 16;
        instruction |=
            static_cast<uint16_t>(immediate);

        return instruction;
    }

    // --------------------------------------------------------
    // B-Type
    //
    // Opcode[6]
    // rs1[5]
    // rs2[5]
    // offset[16]
    // --------------------------------------------------------

    if (info->format == Format::B)
    {
        if (tokens.size() < 4)
            throw std::runtime_error(
                mnemonic + " requires Rs1, Rs2, target"
            );

        int rs1 = parseRegister(tokens[1]);
        int rs2 = parseRegister(tokens[2]);

        if (rs1 < 0 || rs2 < 0)
            throw std::runtime_error(
                "Invalid register in " + mnemonic
            );

        int64_t target;

        auto symbol = symbolTable.find(tokens[3]);

        if (symbol != symbolTable.end())
        {
            target =
                static_cast<int64_t>(symbol->second) -
                static_cast<int64_t>(pc);
        }
        else
        {
            target = parseImmediate(tokens[3]);
        }

        if (target % 4 != 0)
            throw std::runtime_error(
                "Branch target must be 4-byte aligned"
            );

        if (!fitsSigned(target, 16))
            throw std::runtime_error(
                "Branch offset does not fit 16 bits"
            );

        uint32_t instruction = 0;

        instruction |= (opcode & 0x3F) << 26;
        instruction |= (rs1 & 0x1F) << 21;
        instruction |= (rs2 & 0x1F) << 16;
        instruction |=
            static_cast<uint16_t>(target);

        return instruction;
    }

    // --------------------------------------------------------
    // J-Type
    //
    // Opcode[6]
    // rd[5]
    // target[21]
    // --------------------------------------------------------

    if (info->format == Format::J)
    {
        if (tokens.size() < 3)
            throw std::runtime_error(
                mnemonic + " requires Rd, target"
            );

        int rd = parseRegister(tokens[1]);

        if (rd < 0)
            throw std::runtime_error(
                "Invalid destination register"
            );

        int64_t target;

        auto symbol = symbolTable.find(tokens[2]);

        if (symbol != symbolTable.end())
        {
            target =
                static_cast<int64_t>(symbol->second) -
                static_cast<int64_t>(pc);
        }
        else
        {
            target = parseImmediate(tokens[2]);
        }

        if (target % 4 != 0)
            throw std::runtime_error(
                "Jump target must be 4-byte aligned"
            );

        if (!fitsSigned(target, 21))
            throw std::runtime_error(
                "Jump target does not fit 21 bits"
            );

        uint32_t instruction = 0;

        instruction |= (opcode & 0x3F) << 26;
        instruction |= (rd & 0x1F) << 21;

        instruction |=
            static_cast<uint32_t>(target) &
            0x1FFFFF;

        return instruction;
    }

    throw std::runtime_error(
        "Unsupported instruction format"
    );
}


// ------------------------------------------------------------
// Assemble complete program
// ------------------------------------------------------------

std::vector<uint32_t> Assembler::assemble(
    const std::vector<std::string>& source)
{
    // Preprocessing expands synthetic instructions.
    std::vector<std::string> lines =
        preprocess(source);

    // Pass 1:
    // Build label -> address table.
    firstPass(lines);

    // Pass 2:
    // Generate machine code.
    std::vector<uint32_t> machineCode;

    uint32_t pc = 0;

    for (const std::string& line : lines)
    {
        std::string current = trim(line);

        if (current.empty())
            continue;

        // Remove label.
        size_t colon = current.find(':');

        if (colon != std::string::npos)
        {
            current =
                trim(current.substr(colon + 1));

            if (current.empty())
                continue;
        }

        uint32_t instruction =
            secondPassInstruction(current, pc);

        machineCode.push_back(instruction);

        pc += 4;
    }

    return machineCode;
}


// ------------------------------------------------------------
// Assemble file
// ------------------------------------------------------------

void Assembler::assembleFile(
    const std::string& inputFile,
    const std::string& outputFile)
{
    std::ifstream input(inputFile);

    if (!input)
        throw std::runtime_error(
            "Unable to open input file: " + inputFile
        );

    std::vector<std::string> source;
    std::string line;

    while (std::getline(input, line))
        source.push_back(line);

    input.close();

    std::vector<uint32_t> machineCode =
        assemble(source);

    std::ofstream output(outputFile);

    if (!output)
        throw std::runtime_error(
            "Unable to open output file: " + outputFile
        );

    for (uint32_t instruction : machineCode)
    {
        output << std::hex
               << std::uppercase
               << std::setfill('0')
               << std::setw(8)
               << instruction
               << '\n';
    }

    output.close();
}

} // namespace risc201