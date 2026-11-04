#pragma once
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include "MachineState.hpp"

class StackMemoryVisualizer {
public:
    static constexpr int LOWER = 3072;
    static constexpr int UPPER = 4095;
    explicit StackMemoryVisualizer(const MachineState& m) : machine_(m) {
        if (m.memory.size() <= UPPER) throw std::invalid_argument("memory is too small for configured stack region");
    }
    void render(std::ostream& out = std::cout, int rows = 16) const {
        if (rows < 1) throw std::invalid_argument("rows must be positive");
        const int sp = machine_.Reg[2];
        out << "\n========== STACK MEMORY FRAME ==========\n";
        out << "SP (r2): " << sp << " | Bounds: [" << LOWER << ", " << UPPER << "]\n";
        out << "+------------+----------------+----------------+\n";
        out << "| Address    | Value          | Status         |\n";
        out << "+------------+----------------+----------------+\n";
        if (sp < LOWER || sp > UPPER + 1) out << "| INVALID STACK POINTER\n";
        else if (sp == UPPER + 1) out << "| Stack is empty\n";
        else {
            int end = std::min(UPPER, sp + rows - 1);
            for (int a = sp; a <= end; ++a)
                out << "| " << std::left << std::setw(10) << a << " | " << std::setw(14) << machine_.memory[a]
                    << " | " << std::setw(14) << (a == sp ? "TOP <- SP" : "occupied") << " |\n";
            if (end < UPPER) out << "| ... " << (UPPER - end) << " more stack word(s)\n";
        }
        out << "+------------+----------------+----------------+\n";
    }
    std::string render(int rows = 16) const { std::ostringstream s; render(s, rows); return s.str(); }
private:
    const MachineState& machine_;
};
