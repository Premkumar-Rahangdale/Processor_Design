#pragma once
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace std;

class MachineState{
private:
    int numRegisters = 32;
    int memoryWords = 4096;

public:
    //Flags
    bool zero = false;
    bool neg = false;
    bool over = false;
    bool carry = false;

    vector<int> Reg = vector<int>(numRegisters);
    vector<int> memory = vector<int>(memoryWords, 0);
    vector<string> instrMemory;
    unordered_map <string, int> labelAddress; 
    unordered_map <int, string> inverseLabelAddress;
    int pc = 0;

    // Index in instrMemory where execution starts: the first instruction after the ".main" label
    // (taken from the label table built by the assembler), or 0 if the program has no main label.
    int entryPoint() const {
        auto it = labelAddress.find(".main");
        return it != labelAddress.end() ? it->second : 0;
    }

    void init() {
        fill(Reg.begin(), Reg.end(), 0);
        fill(memory.begin(), memory.end(), 0);
        pc = entryPoint();
    }
    void reset(){
        fill(Reg.begin(), Reg.end(), 0);
        fill(memory.begin(), memory.end(), 0);
        pc = entryPoint();   // start at .main, not at the beginning of the machine code
        //all flags and control signals reset
        zero = false;
        neg = false;
        over = false;
        carry = false;
    }
};