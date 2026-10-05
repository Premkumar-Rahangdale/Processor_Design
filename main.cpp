#include <iostream>

#include "include/isa.hpp"
#include "include/MachineState.hpp"
#include "src/assembler.cpp"

using namespace std;

int main(){
    MachineState machine;
    Assembler assembler;
    assembler.first_pass();
    assembler.second_pass();
}