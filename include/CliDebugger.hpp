#pragma once

#include "MachineState.hpp"
#include "Execute.hpp"
#include "isa.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <climits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

class CliDebugger {
private:
    MachineState& machine;
    Assembler& assembler;
    CPUExecution cpu;
    set<int> breakpoints;
    bool skipCurrentBreakpoint = false;

    static string instructionName(const string& encoded) {
        if (encoded.size() != 32) {
            return "invalid instruction";
        } 

        try {
            const uint32_t word = static_cast<uint32_t>(stoul(encoded, nullptr, 2));
            const uint32_t opcode = (word >> 27) & 0x1Fu;
            
            auto it = isa::inverseOpcode.find(static_cast<int>(opcode));

            if (it == isa::inverseOpcode.end()) {
                return "invalid opcode";
            }else {
                return it->second;
            }
        } catch (...) { 
            return "invalid instruction"; 
        }
    }

    void showHelp() {
        cout << "Commands:\n";
        cout << "    load                         Assemble and load tests/t_asmblr.txt\n";
        cout << "    step [n]                     Execute one instruction, or n instructions\n";
        cout << "    continue                     Run until a breakpoint or program end\n";
        cout << "    run                          Run until program end or a breakpoint\n";
        cout << "    registers                    Display all registers and flags\n";
        cout << "    state                        Display PC, current instruction, registers and flags\n";
        cout << "    memory <start> [count]       Display data memory words\n";
        cout << "    list [start] [count]         Display instruction addresses and opcodes\n";
        cout << "    break <address>              Set a breakpoint at an instruction address\n";
        cout << "    delete <address>             Remove a breakpoint\n";
        cout << "    breakpoints                  List breakpoints\n";
        cout << "    reset                        Reset registers, memory, flags and PC\n";
        cout << "    help                         Display this help\n";
        cout << "    quit                         Exit the debugger\n";
    }

    void showRegisters() const {
        size_t total_registers = machine.Reg.size();

        for (size_t i = 0; i < total_registers; ++i) {
            cout << "r" << left << setw(2) << i << " = " << right << setw(11) << machine.Reg[i];

            if (i % 4 == 3) {
                cout << '\n'; 
            } else  {
                cout << "    ";
            }
        }

        cout << "PC = " << machine.pc << "\n" << "Flags: Z=" << machine.zero << " N=" << machine.neg << " O=" << machine.over << " C=" << machine.carry << "\n";
    }

    void showState() const {
        cout << "PC: " << machine.pc << "\n";

        if (machine.pc >= 0 && static_cast<size_t>(machine.pc) < machine.instrMemory.size()) {
            cout << "Instruction: " << instructionName(machine.instrMemory[machine.pc]) << " | Machine code: " << machine.instrMemory[machine.pc] << "\n";
        } else {
            cout << "Instruction: <program finished or PC out of range>\n";
        }

        cout << "Flags: Z=" << machine.zero << " N=" << machine.neg << " O=" << machine.over << " C=" << machine.carry << "\n";
    }

    void showMemory(int start, int count) const {
        
        if (start < 0 || count < 1 || start >= static_cast<int>(machine.memory.size())) {
            cout << "Invalid memory range.\n";
            return;
        }
        const int end = min(start + count, static_cast<int>(machine.memory.size()));
        
        for (int address = start; address < end; ++address) {
            cout << "memory[" << address << "] = " << machine.memory[address] << '\n';
        }
    }

    void showInstructions(int start, int count) const {

        if (machine.instrMemory.empty()) {
            cout << "No program loaded. Use: load\n";
            return;
        }
        
        if (start < 0 || count < 1 || start >= static_cast<int>(machine.instrMemory.size())) {
            cout << "Invalid instruction range.\n";
            return;
        }
        const int end = min(start + count, static_cast<int>(machine.instrMemory.size()));
        
        for (int address = start; address < end; ++address) {
            cout << (address == machine.pc ? "> " : "  ") << setw(4) << address << "  " << left << setw(6) << instructionName(machine.instrMemory[address]) 
                 << " " << machine.instrMemory[address] << right;

            if (breakpoints.count(address)) {
                cout << "  [breakpoint]";
            }

            cout << '\n';
        }
    }

    void stepInstructions(int count) {

        if (machine.instrMemory.empty()) {
            cout << "No program loaded. Use: load\n";
            return;
        }

        if (count < 1) { 
            cout << "Step count must be positive.\n"; 
            return; 
        }
        skipCurrentBreakpoint = false;
        
        for (int i = 0; i < count; i++) {

            if (cpu.finished(machine)) { 
                cout << "Execution completed.\n"; 
                return; 
            }
            const int executedPC = machine.pc;
            
            try {
                cpu.executeInstruction(machine);
                cout << "Executed PC " << executedPC << ": " << instructionName(machine.instrMemory[executedPC]) << " -> PC " << machine.pc << '\n';
            } catch (const exception& error) {
                cout << "Runtime error: " << error.what() << '\n';
                return;
            }
            if (cpu.finished(machine)) { 
                cout << "Execution completed.\n"; 
                return; 
            }
        }

        showState();
    }

    void continueExecution() {
        if (machine.instrMemory.empty()) {
            cout << "No program loaded. Use: load\n";
            return;
        }

        if (cpu.finished(machine)) { 
            cout << "Execution completed.\n"; 
            return; 
        }

        bool skipOnce = skipCurrentBreakpoint;
        skipCurrentBreakpoint = false;
        
        while (!cpu.finished(machine)) {
            if (breakpoints.count(machine.pc) && !skipOnce) {
                cout << "Breakpoint reached at PC " << machine.pc << ".\n";
                skipCurrentBreakpoint = true;
                showState();
                return;
            }
            skipOnce = false;
            try {
                cpu.executeInstruction(machine);
            } catch (const exception& error) {
                cout << "Runtime error: " << error.what() << '\n';
                showState();
                return;
            }
        }
        cout << "Execution completed.\n";
        showState();
    }

    static bool parseInt(const string& text, int& value) {
        try {
            size_t used = 0;
            long parsed = stol(text, &used, 0);
            
            if (used != text.size() || parsed < INT_MIN || parsed > INT_MAX) {
                return false;
            }

            value = static_cast<int>(parsed);
            return true;
        } catch (...) { 
            return false; 
        }
    }

public:
    CliDebugger(MachineState& state, Assembler& assemblerRef) : machine(state), assembler(assemblerRef) {}

    void run() {

        cout << "RISC201 CPU Step-by-Step Debugger\nType 'help' for commands.\n";
        string line;
        
        while (true) {
            cout << "debug> " << flush;
            if (!getline(cin, line)) 
                break;

            istringstream input(line);
            
            string command;
            input >> command;
            
            if (command.empty()) {
                continue;
            }

            if (command == "help") {
                showHelp();
            } 
            else if (command == "quit" || command == "exit") {
                break;
            } 
            else if (command == "load") {
                
                string extra;
                
                if (input >> extra) {
                    cout << "The existing Assembler reads tests/t_asmblr.txt only. Put the program there, then use: load\n";
                    continue;
                }
                
                try {
                    machine.instrMemory.clear();
                    machine.labelAddress.clear();
                    machine.inverseLabelAddress.clear();
                    assembler.assemble(machine);
                    machine.reset();
                    breakpoints.clear();
                    skipCurrentBreakpoint = false;
                    cout << "Program loaded from tests/t_asmblr.txt (" << machine.instrMemory.size() << " instructions).\n";
                    showState();
                } catch (const exception& error) {
                    cout << "Load failed: " << error.what() << '\n';
                }
            }
            else if (command == "step") {
                string countText;
                int count = 1;
                
                if (input >> countText && !parseInt(countText, count)) { 
                    cout << "Invalid step count.\n"; 
                    continue; 
                }

                stepInstructions(count);
            }
            else if (command == "continue" || command == "run") {
                continueExecution();
            } 
            else if (command == "registers") {
                showRegisters();
            } 
            else if (command == "state") {
                showState();
            }
            else if (command == "memory") {
                string a, c;
                int start = 0, count = 1;
                
                if (!(input >> a) || !parseInt(a, start)) { 
                    cout << "Usage: memory <start> [count]\n"; 
                    continue;
                }
                
                if (input >> c) { 
                    if (!parseInt(c, count)) {
                        cout << "Invalid count.\n";
                        continue; 
                    } 
                }
                
                showMemory(start, count);
            }
            else if (command == "list") {
                string a, c;
                int start = 0, count = 10;
                
                if (input >> a) { 
                    if (!parseInt(a, start)) { 
                        cout << "Invalid start address.\n"; 
                        continue; 
                    } 
                }

                if (input >> c) { 
                    if (!parseInt(c, count)) { 
                        cout << "Invalid count.\n"; 
                        continue; 
                    } 
                }

                showInstructions(start, count);
            }
            else if (command == "break") {
                
                string a; int address;
                
                if (!(input >> a) || !parseInt(a, address) || address < 0 || address >= static_cast<int>(machine.instrMemory.size())) {
                    cout << "Usage: break <valid instruction address>\n"; continue;
                }

                breakpoints.insert(address);
                
                cout << "Breakpoint set at PC " << address << ".\n";
            }
            else if (command == "delete") {
                
                string a; int address;
                
                if (!(input >> a) || !parseInt(a, address)) { 
                    cout << "Usage: delete <address>\n"; 
                    continue; 
                }

                if (breakpoints.erase(address)) {
                    cout << "Breakpoint removed.\n";
                } else {
                    cout << "No breakpoint at that address.\n";
                }
            }
            else if (command == "breakpoints") {
                if (breakpoints.empty()) {
                    cout << "No breakpoints set.\n";
                }
                else {
                    for (int address : breakpoints) {
                        cout << address << '\n';
                    }
                } 
            }
            else if (command == "reset") {
                machine.reset();
                skipCurrentBreakpoint = false;
                cout << "CPU state reset. Program remains loaded.\n";
                showState();
            }
            else {
                cout << "Unknown command. Type 'help'.\n";
            }
        }
        cout << "Debugger exited.\n";
    }
};
