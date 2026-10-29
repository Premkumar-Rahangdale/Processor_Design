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

    void init() {
        fill(Reg.begin(), Reg.end(), 0);
        fill(memory.begin(), memory.end(), 0);
        pc = 0;
    }
    void reset(){
        fill(Reg.begin(), Reg.end(), 0);
        fill(memory.begin(), memory.end(), 0);
        pc = 0;
        //all flags and control signals reset
        zero = false;
        neg = false;
        over = false;
        carry = false;
    }
};



