#pragma once
#include "isa.hpp"
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <algorithm>

constexpr int Num_Registers = 32;
constexpr int Memory_Words = 4096;

struct Flags{
    bool zero = false;
    bool neg = false;
    bool over = false;
    bool carry = false;
};

struct MachineState{
    // may require refactoring later
    std::array<std::uint32_t,Num_Registers> Reg;
    std::vector<std::uint32_t> memory = std::vector<std::uint32_t>(Memory_Words, 0u);
    std::uint32_t pc = 0;
    Flags flags{};
    void reset(){
        Reg.fill(0);
        std::fill(memory.begin(),memory.end(),0);
        pc = 0;
        flags = Flags{};
    }
};
