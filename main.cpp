#include "include/isa.hpp"
#include "include/MachineState.hpp"
#include "src/assembler.cpp"
#include "src/disassembler.cpp"
#include "include/pipeline6stage.hpp"
#include "src/controlunit.cpp"
#include "src/alu.cpp"
#include "src/pipeline6stage.cpp"

using namespace std;

int main(){
    MachineState machine;
    Assembler assembler;
    assembler.assemble(machine);
    Disassembler disassembler;
    disassembler.disassembler(machine);
    Pipeline6stage pipeline;
    pipeline.run(machine);
}