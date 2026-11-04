#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class StackPreprocessor {
public:
    static constexpr int STACK_LOWER = 3072;
    static constexpr int STACK_UPPER = 4095;
    static constexpr int RETURN_VALUE_REGISTER = 31;

    static bool enabled() { return stackEnabled_; }

    static std::vector<std::string> expand(const std::vector<std::string>& source) {
        stackEnabled_ = false;
        std::vector<std::string> code;
        code.reserve(source.size());
        for (const auto& line : source) code.push_back(stripComment(line));

        std::unordered_set<std::string> functionEntries;
        functionEntries.insert(".main");
        for (const auto& line : code) {
            auto t = tokens(line);
            for (std::size_t i = 0; i + 1 < t.size(); ++i) {
                if (lower(t[i]) == "call") functionEntries.insert(normalizeLabel(t[i + 1]));
            }
        }

        std::unordered_map<std::string, std::set<int>> savedRegisters;
        std::vector<std::pair<std::string, std::size_t>> entries;
        for (std::size_t i = 0; i < code.size(); ++i) {
            auto label = labelOnLine(code[i]);
            if (!label.empty() && functionEntries.count(label)) entries.push_back({label, i});
        }
        if (entries.empty() && !code.empty()) entries.push_back({".main", 0});
        if (!entries.empty() && entries.front().second != 0 && functionEntries.count(".main"))
            entries.insert(entries.begin(), {".main", 0});

        for (std::size_t f = 0; f < entries.size(); ++f) {
            const std::size_t begin = entries[f].second;
            const std::size_t end = (f + 1 < entries.size()) ? entries[f + 1].second : code.size();
            std::set<int> used;
            bool hasCall = false;
            for (std::size_t i = begin; i < end; ++i) {
                auto t = tokens(code[i]);
                if (t.empty()) continue;
                static const std::regex regPattern(R"((?:^|[^A-Za-z0-9_])(r(?:[0-9]|[12][0-9]|3[01])|ra|sp)(?=$|[^A-Za-z0-9_]))", std::regex::icase);
                for (std::sregex_iterator it(code[i].begin(), code[i].end(), regPattern), last; it != last; ++it) {
                    int reg = registerNumber((*it)[1].str());
                    if (reg > 0 && reg != 2 && reg != RETURN_VALUE_REGISTER) used.insert(reg);
                }
                if (lower(t[0]) == "call") hasCall = true;
            }
            if (hasCall) used.insert(1); // r1/ra holds this function's own return address.
            savedRegisters[entries[f].first] = std::move(used);
        }

        // Function bodies placed after .main must not be executed by ordinary
        // fall-through when main reaches its end.  The ISA has no HALT opcode,
        // so branch around the out-of-line function bodies to a final label.
        std::size_t mainLine = 0;
        for (const auto& entry : entries)
            if (entry.first == ".main") { mainLine = entry.second; break; }
        std::size_t firstFunctionLine = code.size();
        for (const auto& entry : entries) {
            if (entry.first != ".main" && entry.second > mainLine) {
                firstFunctionLine = entry.second;
                break;
            }
        }
        std::string endLabel = ".end";
        while (std::any_of(code.begin(), code.end(), [&](const std::string& line) {
            return labelOnLine(line) == endLabel;
        })) endLabel += "_";

        std::vector<std::string> output;
        bool insertedMainExit = false;
        for (std::size_t i = 0; i < code.size(); ++i) {
            std::string line = trim(code[i]);
            if (line.empty()) continue;
            auto t = tokens(line);
            if (t.empty()) continue;
            const std::string op = lower(t[0]);

            if (op == "call") {
                stackEnabled_ = true;
                const std::string caller = ownerAt(entries, i);
                const auto it = savedRegisters.find(caller);
                if (it != savedRegisters.end()) {
                    for (int reg : it->second) { output.push_back("sub r2 r2 1"); output.push_back("st r" + std::to_string(reg) + " 0[r2]"); }
                }
                output.push_back(line);
                if (it != savedRegisters.end()) {
                    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) { output.push_back("ld r" + std::to_string(*rit) + " 0[r2]"); output.push_back("add r2 r2 1"); }
                }
                continue;
            }

            if (op == "push" || op == "pop") {
                stackEnabled_ = true;
                if (t.size() != 2 || registerNumber(t[1]) < 0)
                    throw std::runtime_error(op + " expects exactly one valid register");
                const int reg = registerNumber(t[1]);
                if (reg == 0 || reg == 2)
                    throw std::runtime_error("push/pop cannot use r0 or r2 (r2 is the stack pointer)");
                if (op == "push") {
                    output.push_back("sub r2 r2 1");
                    output.push_back("st r" + std::to_string(reg) + " 0[r2]");
                } else {
                    output.push_back("ld r" + std::to_string(reg) + " 0[r2]");
                    output.push_back("add r2 r2 1");
                }
                continue;
            }

            const std::string currentLabel = labelOnLine(line);
            if (!insertedMainExit && i == firstFunctionLine && firstFunctionLine < code.size()) {
                output.push_back("b " + endLabel);
                insertedMainExit = true;
            }
            if (!currentLabel.empty()) {
                output.push_back(line);
                if (currentLabel != ".main" && functionEntries.count(currentLabel)) {
                    stackEnabled_ = true;
                    output.push_back("sub r2 r2 1");
                    output.push_back("st r1 0[r2]");
                }
                continue;
            }

            if (op == "ret") {
                stackEnabled_ = true;
                const std::string currentFunction = ownerAt(entries, i);
                if (currentFunction != ".main") {
                    output.push_back("ld r1 0[r2]");
                    output.push_back("add r2 r2 1");
                }
            }
            output.push_back(line);
        }
        if (insertedMainExit) output.push_back(endLabel + ":");
        return output;
    }

    static void expandFile(const std::string& inputPath, const std::string& outputPath) {
        std::ifstream in(inputPath);
        if (!in) throw std::runtime_error("cannot open assembly source: " + inputPath);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        const auto expanded = expand(lines);
        std::ofstream out(outputPath);
        if (!out) throw std::runtime_error("cannot write preprocessed assembly: " + outputPath);
        for (const auto& item : expanded) out << item << '\n';
    }

private:
    inline static bool stackEnabled_ = false;

    static std::string trim(const std::string& s) {
        const auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        const auto b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    static std::string stripComment(const std::string& s) {
        const auto p = s.find("//");
        return trim(p == std::string::npos ? s : s.substr(0, p));
    }
    static std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
    static std::vector<std::string> tokens(const std::string& s) {
        std::istringstream in(s);
        std::vector<std::string> out;
        std::string t;
        while (in >> t) out.push_back(t);
        return out;
    }
    static std::string normalizeLabel(std::string s) {
        if (!s.empty() && s.back() == ':') s.pop_back();
        return s;
    }
    static std::string labelOnLine(const std::string& s) {
        auto t = tokens(s);
        if (t.size() == 1 && !t[0].empty() && t[0][0] == '.') return normalizeLabel(t[0]);
        return {};
    }
    static int registerNumber(const std::string& token) {
        std::string s = lower(token);
        if (s == "ra") return 1;
        if (s == "sp") return 2;
        if (s.size() < 2 || s[0] != 'r') return -1;
        try {
            std::size_t used = 0;
            int n = std::stoi(s.substr(1), &used);
            if (used != s.size() - 1 || n < 0 || n > 31) return -1;
            return n;
        } catch (...) { return -1; }
    }
    static std::string ownerAt(const std::vector<std::pair<std::string, std::size_t>>& entries, std::size_t line) {
        std::string owner = entries.empty() ? ".main" : entries.front().first;
        for (const auto& e : entries) {
            if (e.second > line) break;
            owner = e.first;
        }
        return owner;
    }
};
