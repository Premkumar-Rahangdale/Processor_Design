#pragma once
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "MachineState.hpp"

class StackMemoryVisualizer {
public:
    explicit StackMemoryVisualizer(const MachineState& state,
                                   int lowerBound = 0,
                                   int upperBound = -1)
        : machine_(state), lower_(lowerBound),
          upper_(upperBound < 0 ? static_cast<int>(state.memory.size()) - 1 : upperBound) {
        if (machine_.memory.empty() || lower_ < 0 ||
            upper_ >= static_cast<int>(machine_.memory.size()) || lower_ > upper_)
            throw std::invalid_argument("invalid stack visualizer boundaries");
    }

    void render(std::ostream& out = std::cout, int rows = 16) const {
        if (rows < 1) throw std::invalid_argument("visualizer rows must be positive");

        const int sp = machine_.Reg[2];
        out << "\n========== RISC201 STACK MEMORY ==========\n";
        out << "Direction: full-descending (towards lower addresses)\n";
        out << "SP (r2): " << sp << "    Stack bounds: [" << lower_ << ", " << upper_ << "]\n";
        out << "+------------+----------------+----------------+\n";
        out << "| Address    | Value          | Status         |\n";
        out << "+------------+----------------+----------------+\n";

        if (sp < lower_ || sp > upper_ + 1) {
            out << "| INVALID STACK POINTER: " << sp << "\n";
        } else if (sp == upper_ + 1) {
            out << "| Stack is empty (SP is one past the upper boundary).\n";
        } else {
            const int end = std::min(upper_, sp + rows - 1);
            for (int address = sp; address <= end; ++address) {
                std::ostringstream value;
                value << machine_.memory[address];
                out << "| " << std::left << std::setw(10) << address
                    << " | " << std::setw(14) << value.str()
                    << " | " << std::setw(14) << (address == sp ? "TOP <- SP" : "occupied")
                    << " |\n";
            }
            if (end < upper_)
                out << "| ... " << (upper_ - end) << " more stack word(s)\n";
        }

        out << "+------------+----------------+----------------+\n";
        out << "==========================================\n";
    }

    std::string render(int rows = 16) const {
        std::ostringstream output;
        render(output, rows);
        return output.str();
    }

private:
    const MachineState& machine_;
    int lower_;
    int upper_;
};
