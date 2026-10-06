#include "include/isa.hpp"
#include "include/MachineState.hpp"
#include "src/assembler.cpp"
#include "src/disassembler.cpp"
// #include "include/pipeline6stage.hpp"

using namespace std;

int main(){
    MachineState machine;
    Assembler assembler;
    assembler.assemble(machine);
    Disassembler disassembler;
    disassembler.disassembler(machine);
}