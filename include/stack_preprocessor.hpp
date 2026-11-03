#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class StackPreprocessor {
public:
    static std::vector<std::string> expand(const std::vector<std::string>& source) {
        std::vector<std::string> output;

        for (const std::string& original : source) {
            std::string code = original;
            std::string comment;
            const std::size_t commentPos = code.find("//");
            if (commentPos != std::string::npos) {
                comment = code.substr(commentPos);
                code.erase(commentPos);
            }

            std::istringstream input(code);
            std::vector<std::string> tokens;
            std::string token;
            while (input >> token) tokens.push_back(token);

            if (tokens.empty()) {
                if (!comment.empty()) output.push_back(comment);
                else if (!original.empty()) output.push_back(original);
                continue;
            }

            std::size_t opIndex = 0;
            std::string labelPrefix;
            if (!tokens[0].empty() && tokens[0].back() == ':') {
                labelPrefix = tokens[0];
                opIndex = 1;
            }

            if (opIndex >= tokens.size()) {
                output.push_back(original);
                continue;
            }

            std::string operation = tokens[opIndex];
            std::transform(operation.begin(), operation.end(), operation.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            const bool isPush = operation == "push";
            const bool isPop = operation == "pop";
            if (!isPush && !isPop) {
                output.push_back(original);
                continue;
            }

            if (tokens.size() != opIndex + 2)
                throw std::runtime_error("stack macro '" + operation + "' expects exactly one register");

            const std::string reg = tokens[opIndex + 1];
            if (!isRegister(reg))
                throw std::runtime_error("invalid register in stack macro: " + reg);
            if (isZeroRegister(reg))
                throw std::runtime_error("r0 cannot be used as a stack macro source/destination");

            if (!labelPrefix.empty()) output.push_back(labelPrefix);

            if (isPush) {
                output.push_back("sub r2 r2 1");
                output.push_back("st " + reg + " 0[r2]");
            } else {
                output.push_back("ld " + reg + " 0[r2]");
                output.push_back("add r2 r2 1");
            }

            if (!comment.empty()) output.back() += " " + comment;
        }

        return output;
    }

    static void expandFile(const std::string& inputPath, const std::string& outputPath) {
        std::ifstream input(inputPath);
        if (!input) throw std::runtime_error("cannot open stack-preprocessor input: " + inputPath);

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line)) lines.push_back(line);
        input.close();

        const std::vector<std::string> expanded = expand(lines);
        std::ofstream output(outputPath);
        if (!output) throw std::runtime_error("cannot open stack-preprocessor output: " + outputPath);
        for (const std::string& expandedLine : expanded) output << expandedLine << '\n';
    }

private:
    static bool isZeroRegister(const std::string& value) {
        return value == "r0" || value == "R0";
    }

    static bool isRegister(const std::string& value) {
        if (value == "sp" || value == "SP" || value == "ra" || value == "RA") return true;
        if (value.size() < 2 || (value[0] != 'r' && value[0] != 'R')) return false;
        try {
            std::size_t used = 0;
            const int number = std::stoi(value.substr(1), &used);
            return used == value.size() - 1 && number >= 0 && number <= 31;
        } catch (...) {
            return false;
        }
    }
};
