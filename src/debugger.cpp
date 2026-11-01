#pragma once
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// P.E.K.K.A. debug mode.
//
// Runs the assembled program a few instructions at a time on the existing
// Pipeline6stage (begin / step / stepN) and shows what each step changed.
// Include this file after pipeline6stage.cpp and before pekka.cpp.
//
// Commands:  step [n] | first <n> | registers | memory | state | help | exit
class PekkaDebugger
{
private:
    MachineState &machine;
    Pipeline6stage pipeline;
    vector<string> source;   // source[i] = assembly text of instrMemory[i] (tests/t_asmblr.txt)

    // what the machine looked like before a command, to find out what changed
    struct Snapshot
    {
        vector<int> reg;
        vector<int> mem;
        bool zero, neg, over, carry;
        int pc;
    };

    static const int kMaxListed = 64;   // longest list of changes printed

    const string RESET = "\033[0m";
    const string BOLD = "\033[1m";
    const string DIM = "\033[2m";
    const string RED = "\033[31m";
    const string GREEN = "\033[32m";
    const string YELLOW = "\033[33m";
    const string CYAN = "\033[36m";

    void ok(const string &msg) { cout << GREEN << "[ OK ] " << RESET << msg << "\n"; }
    void fail(const string &msg) { cout << RED << "[FAIL] " << RESET << msg << "\n"; }
    void info(const string &msg) { cout << CYAN << "[INFO] " << RESET << msg << "\n"; }

    void prompt()
    {
        cout << GREEN << BOLD << "pekka" << RESET << YELLOW << BOLD << "(debug)" << RESET << "$ " << flush;
    }

    // ------------------------------------------------------------------ helpers

    // The pipeline only needs the 32-bit words; the text is read for display only.
    void load_source_text()
    {
        source.clear();
        ifstream in("tests/t_asmblr.txt");
        string line;
        while (getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            source.push_back(line);
        }
        if (source.size() != machine.instrMemory.size())
            source.clear();   // does not belong to this program, show machine code instead
    }

    string text_of(int pc) const
    {
        if (pc >= 0 && pc < (int)source.size())
            return source[pc];
        if (pc >= 0 && pc < (int)machine.instrMemory.size())
            return machine.instrMemory[pc];
        return "?";
    }

    static string reg_name(int i)
    {
        if (i == 1)
            return "r1/ra";
        if (i == 2)
            return "r2/sp";
        return "r" + to_string(i);
    }

    // pc as shown to the user: the next real instruction (machine.pc itself may
    // point at a label line, which the pipeline skips on the next step).
    int current_pc() const
    {
        int next = pipeline.nextPc(machine);
        return next >= 0 ? next : machine.pc;
    }

    Snapshot take() const
    {
        return Snapshot{machine.Reg, machine.memory, machine.zero, machine.neg, machine.over, machine.carry, current_pc()};
    }

    void show_next()
    {
        int next = pipeline.nextPc(machine);
        if (next >= 0)
            cout << DIM << "next  " << RESET << "pc=" << next << "  " << BOLD << text_of(next) << RESET << "\n";
        else if (pipeline.faulted())
            cout << RED << "stopped by a fault (message above)" << RESET << " - use 'first <n>' to run again\n";
        else
            cout << YELLOW << "program finished" << RESET << " (pc=" << machine.pc << ", past the last instruction) - use 'first <n>' to run again\n";
    }

    // Lists the instructions that were executed and everything they changed.
    void report(const Snapshot &before, const vector<int> &executedPcs)
    {
        cout << BOLD << "executed " << executedPcs.size() << (executedPcs.size() == 1 ? " instruction" : " instructions") << RESET << "\n";
        int shown = 0;
        for (int pc : executedPcs)
        {
            if (shown++ == kMaxListed)
            {
                cout << "  ... " << executedPcs.size() - kMaxListed << " more\n";
                break;
            }
            cout << "  pc=" << setw(3) << left << pc << right << "  " << text_of(pc) << "\n";
        }

        // registers
        vector<string> lines;
        for (size_t i = 0; i < machine.Reg.size(); i++)
            if (machine.Reg[i] != before.reg[i])
                lines.push_back(reg_name((int)i) + "  " + to_string(before.reg[i]) + " -> " + to_string(machine.Reg[i]));
        // memory
        for (size_t i = 0; i < machine.memory.size(); i++)
            if (machine.memory[i] != before.mem[i])
                lines.push_back("mem[" + to_string(i) + "]  " + to_string(before.mem[i]) + " -> " + to_string(machine.memory[i]));

        if (lines.empty())
            cout << DIM << "registers / memory: no change" << RESET << "\n";
        else
        {
            cout << "changed:\n";
            int count = 0;
            for (const string &l : lines)
            {
                if (count++ == kMaxListed)
                {
                    cout << "  ... " << lines.size() - kMaxListed << " more\n";
                    break;
                }
                cout << "  " << GREEN << l << RESET << "\n";
            }
        }

        // flags
        string flags;
        auto flag = [&](const char *name, bool was, bool now) {
            if (was != now)
                flags += string(" ") + name + " " + (was ? "1" : "0") + "->" + (now ? "1" : "0");
        };
        flag("Z", before.zero, machine.zero);
        flag("N", before.neg, machine.neg);
        flag("O", before.over, machine.over);
        flag("C", before.carry, machine.carry);
        if (!flags.empty())
            cout << "flags:" << GREEN << flags << RESET << "\n";

        cout << "pc  " << before.pc << " -> " << current_pc() << "\n";
        show_next();
    }

    // Runs n instructions from the current pc and reports the changes.
    void execute(int n)
    {
        if (pipeline.finished(machine))
        {
            show_next();
            return;
        }
        Snapshot before = take();
        vector<int> executedPcs;
        cout << flush;   // keep the pipeline's own fault message in order
        pipeline.stepN(machine, n, &executedPcs);

        if (executedPcs.empty())
        {
            show_next();   // fault in the very first instruction
            return;
        }
        report(before, executedPcs);
    }

    // Reads the optional / required count of "step [n]" and "first <n>".
    bool parse_count(const string &arg, bool required, int &n)
    {
        if (arg.empty())
        {
            if (required)
            {
                fail("number of instructions needed, e.g. 'first 5'");
                return false;
            }
            n = 1;
            return true;
        }
        for (char c : arg)
            if (!isdigit((unsigned char)c))
            {
                fail("'" + arg + "' is not a positive number");
                return false;
            }
        try
        {
            n = stoi(arg);
        }
        catch (...)
        {
            fail("'" + arg + "' is too large");
            return false;
        }
        if (n < 1)
        {
            fail("number of instructions must be at least 1");
            return false;
        }
        return true;
    }

    // ----------------------------------------------------------------- commands

    void cmd_help()
    {
        cout << BOLD << "Debug commands" << RESET << "\n";
        cout << "  " << CYAN << "step" << RESET << "          execute the next instruction (from the current pc)\n";
        cout << "  " << CYAN << "step <n>" << RESET << "      execute the next n instructions\n";
        cout << "  " << CYAN << "first <n>" << RESET << "     restart from the first instruction and execute n instructions\n";
        cout << "  " << CYAN << "registers" << RESET << "     show all registers and flags\n";
        cout << "  " << CYAN << "memory" << RESET << "        show every memory location that holds data (non-zero)\n";
        cout << "  " << CYAN << "state" << RESET << "         show pc, next instruction and flags\n";
        cout << "  " << CYAN << "help" << RESET << "          show this list\n";
        cout << "  " << CYAN << "exit" << RESET << "          leave debug mode\n";
    }

    void cmd_registers()
    {
        for (size_t i = 0; i < machine.Reg.size(); i++)
        {
            cout << (machine.Reg[i] != 0 ? BOLD : DIM) << left << setw(6) << reg_name((int)i) << "= " << right << setw(11) << machine.Reg[i] << RESET;
            cout << (i % 4 == 3 ? "\n" : "   ");
        }
        cout << "pc = " << current_pc() << "    flags: Z=" << machine.zero << " N=" << machine.neg << " O=" << machine.over << " C=" << machine.carry << "\n";
    }

    void cmd_memory()
    {
        int used = 0;
        for (size_t i = 0; i < machine.memory.size(); i++)
            if (machine.memory[i] != 0)
            {
                cout << "mem[" << i << "] = " << machine.memory[i] << "\n";
                used++;
            }
        if (used == 0)
            cout << DIM << "no memory location holds data" << RESET << "\n";
        else
            cout << DIM << used << " of " << machine.memory.size() << " locations in use" << RESET << "\n";
    }

    void cmd_state()
    {
        cout << "pc = " << current_pc() << "    executed: " << pipeline.instructionsExecuted()
             << "    flags: Z=" << machine.zero << " N=" << machine.neg << " O=" << machine.over << " C=" << machine.carry << "\n";
        show_next();
    }

public:
    explicit PekkaDebugger(MachineState &m) : machine(m) {}

    void run()
    {
        pipeline.trace = false;   // the debugger prints its own report
        pipeline.begin(machine);  // registers, memory, flags, pc = 0
        load_source_text();

        int instructions = 0;
        for (const string &word : machine.instrMemory)
            if (!word.empty() && word[0] != '.')
                instructions++;

        ok("debug mode (" + to_string(instructions) + " instructions) - 'help' lists the commands");
        show_next();

        string line;
        while (true)
        {
            prompt();
            if (!getline(cin, line))
            {
                cout << "\n";
                return;
            }

            istringstream words(line);
            string cmd, arg, extra;
            words >> cmd >> arg >> extra;
            transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return (char)tolower(c); });
            if (cmd.empty())
                continue;
            if (!extra.empty())
            {
                fail("too many arguments (type 'help')");
                continue;
            }

            int n = 0;
            bool takes_arg = (cmd == "step" || cmd == "first");
            if (!arg.empty() && !takes_arg)
            {
                fail("'" + cmd + "' takes no arguments");
                continue;
            }

            if (cmd == "step")
            {
                if (parse_count(arg, false, n))
                    execute(n);
            }
            else if (cmd == "first")
            {
                if (parse_count(arg, true, n))
                {
                    pipeline.begin(machine);
                    info("restarted from the first instruction");
                    execute(n);
                }
            }
            else if (cmd == "registers")
                cmd_registers();
            else if (cmd == "memory")
                cmd_memory();
            else if (cmd == "state")
                cmd_state();
            else if (cmd == "help")
                cmd_help();
            else if (cmd == "exit")
                return;
            else
                fail("command not found: " + cmd + "  (type 'help')");
        }
    }
};
