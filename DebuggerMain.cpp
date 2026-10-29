#include "include/isa.hpp"
#include "include/MachineState.hpp"
#include "src/assembler.cpp"
#include "include/Execute.hpp"
#include "include/CliDebugger.hpp"

int main() {
    MachineState machine;
    Assembler assembler;
    CliDebugger debugger(machine, assembler);
    debugger.run();
    return 0;
}
