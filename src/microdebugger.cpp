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

// P.E.K.K.A. micro debugger: steps the horizontal / vertical micro control unit one
// micro-instruction (= one clock of its data path) at a time and shows the micro-PC state.
//
// For every micro-instruction it prints
//   * the micro-PC it was fetched from, the micro-instruction and what the micro-fetch
//     multiplexer chose as the next micro-PC (next / branch / switch / cond / exception),
//   * the micro-registers that changed, and the registers / memory / flags that changed,
//   * (optional, 'words') the control word as stored in the control memory.
//
// Include this file after microcontrolunit.cpp and before pekka.cpp (see PekkaMain.cpp).
//
// Commands:  ustep [n] | step [n] | run | restart | state | uregs | registers | memory | stack |
//            listing | words | clear | help | exit
class PekkaMicroDebugger
{
private:
    MachineState &machine;
    MicroControlUnit &unit;
    vector<string> source;   // source[i] = assembly text of instrMemory[i] (tests/t_asmblr.txt)
    int current = -1;        // index in instrMemory of the instruction being executed (-1: none yet)
    bool showWords = false;  // print the control word of every micro-instruction

    // everything a micro-instruction can change
    struct Snapshot
    {
        vector<uint32_t> micro;
        vector<int> reg;
        vector<int> mem;
        bool zero, neg, over, carry;
    };

    const string RESET = "\033[0m";
    const string BOLD = "\033[1m";
    const string DIM = "\033[2m";
    const string RED = "\033[31m";
    const string GREEN = "\033[32m";
    const string YELLOW = "\033[33m";
    const string CYAN = "\033[36m";

    void ok(const string &msg) { cout << GREEN << "[ OK ] " << RESET << msg << "\n"; }
    void fail(const string &msg) { cout << RED << "[FAIL] " << RESET << msg << "\n"; }
    void warn(const string &msg) { cout << YELLOW << "[WARN] " << RESET << msg << "\n"; }
    void info(const string &msg) { cout << CYAN << "[INFO] " << RESET << msg << "\n"; }

    void prompt()
    {
        cout << GREEN << BOLD << "pekka" << RESET << YELLOW << BOLD << "(" << unit.name() << "-upc)" << RESET << "$ " << flush;
    }

    // ------------------------------------------------------------------ helpers

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

    static string addr4(int a)
    {
        string s = to_string(a);
        return string(s.size() < 4 ? 4 - s.size() : 0, '0') + s;
    }

    Snapshot take() const
    {
        Snapshot s{vector<uint32_t>(kNumMRegs, 0), machine.Reg, machine.memory, machine.zero, machine.neg, machine.over, machine.carry};
        if (const MicroDatapath *dp = unit.datapath())
            for (int i = 0; i < kNumMRegs; i++)
                s.micro[i] = dp->reg(static_cast<MReg>(i));
        return s;
    }

    // the micro-fetch multiplexer setting of a micro-instruction, before it executes
    static const char *fetch_of(MicroOp op)
    {
        switch (op)
        {
        case MicroOp::MB:
            return "branch";
        case MicroOp::MSWITCH:
            return "switch";
        case MicroOp::MBEQ:
            return "cond";
        default:
            return "next";
        }
    }

    string target_text(int addr) const { return addr4(addr) + " " + unit.describeAddress(addr); }

    // ------------------------------------------------------------- printing

    // Lists what changed between two snapshots: micro-registers, then registers / memory / flags.
    void print_changes(const Snapshot &a, const Snapshot &b)
    {
        string micro;
        for (int i = 0; i < kNumMRegs; i++)
            if (a.micro[i] != b.micro[i])
                micro += string(mregName(static_cast<MReg>(i))) + " " + to_string((int32_t)a.micro[i]) + "->" + to_string((int32_t)b.micro[i]) + "   ";
        if (!micro.empty())
            cout << "        " << DIM << "micro-regs: " << RESET << micro << "\n";

        string arch;
        for (size_t i = 0; i < b.reg.size(); i++)
            if (a.reg[i] != b.reg[i])
                arch += reg_name((int)i) + " " + to_string(a.reg[i]) + "->" + to_string(b.reg[i]) + "   ";
        for (size_t i = 0; i < b.mem.size(); i++)
            if (a.mem[i] != b.mem[i])
                arch += "mem[" + to_string(i) + "] " + to_string(a.mem[i]) + "->" + to_string(b.mem[i]) + "   ";
        auto flag = [&](const char *name, bool was, bool now) {
            if (was != now)
                arch += string(name) + " " + (was ? "1" : "0") + "->" + (now ? "1" : "0") + "   ";
        };
        flag("Z", a.zero, b.zero);
        flag("N", a.neg, b.neg);
        flag("O", a.over, b.over);
        flag("C", a.carry, b.carry);
        if (!arch.empty())
            cout << "        " << GREEN << "machine:    " << arch << RESET << "\n";
    }

    // One executed micro-instruction.
    void print_step(const MicroStep &st, const Snapshot &before, const Snapshot &after)
    {
        // A new instruction was fetched by the preamble's mloadIR: pc then holds its index.
        if (st.upc == unit.beginAddress() && st.instr.op == MicroOp::MLOADIR && !st.finished)
        {
            current = machine.pc;
            cout << BOLD << "--- instruction pc=" << current << ":  " << text_of(current) << RESET << "\n";
        }

        cout << "  " << DIM << "#" << setw(4) << left << unit.microInstructionsExecuted() << right << RESET
             << "uPC " << BOLD << addr4(st.upc) << RESET << "  " << CYAN << left << setw(26) << st.text << right << RESET
             << DIM << setw(8) << fetch_of(st.instr.op) << RESET << "  ";
        if (st.finished)
            cout << "end of run";
        else
            cout << st.mux << " -> " << target_text(st.nextUpc);
        cout << DIM << "   clk " << st.cycles << RESET;
        if (!st.instr.label.empty())
            cout << DIM << "  [" << st.instr.label << "]" << RESET;
        if (st.exception)
            cout << RED << "  EXCEPTION" << RESET;
        else if (st.redirect)
            cout << DIM << "  (+" << unit.fetchStages() - 1 << " bubble" << (unit.fetchStages() - 1 == 1 ? "" : "s") << ")" << RESET;
        cout << "\n";

        if (showWords)
            cout << "        " << DIM << "word: " << st.word << RESET << "\n";
        print_changes(before, after);
    }

    void print_finish()
    {
        if (unit.faulted())
            fail("stopped by an exception (cause " + to_string(unit.faultCause()) + ", R4 = cause, R5 = index of the faulting instruction)");
        else if (unit.hitInstructionLimit())
            warn("stopped at the instruction limit");
        else
            ok("program finished - 'restart' runs it again");
        print_totals();
    }

    void print_totals()
    {
        const long instr = unit.instructionsExecuted();
        cout << "  instructions " << BOLD << instr << RESET << "    micro-instructions " << BOLD << unit.microInstructionsExecuted() << RESET
             << "    clocks " << BOLD << unit.cycles() << RESET << "    CPI " << BOLD << fixed << setprecision(2)
             << (instr ? double(unit.cycles()) / double(instr) : 0.0) << RESET << "\n";
        cout << defaultfloat;
    }

    // Executes one micro-instruction and prints it. Returns false when the run is over.
    bool micro_step()
    {
        Snapshot before = take();
        MicroStep st;
        if (!unit.stepMicro(&st))
            return false;
        print_step(st, before, take());
        return !st.finished;
    }

    void not_running()
    {
        warn("the program is finished - use 'restart' to run it again");
    }

    // ------------------------------------------------------------------ commands

    // ustep [n]: n micro-instructions
    void cmd_ustep(int n)
    {
        if (!unit.active())
        {
            not_running();
            return;
        }
        for (int i = 0; i < n; i++)
            if (!micro_step())
            {
                print_finish();
                return;
            }
    }

    // step [n]: n whole instructions (every micro-instruction up to the next preamble)
    void cmd_step(int n)
    {
        if (!unit.active())
        {
            not_running();
            return;
        }
        for (int i = 0; i < n; i++)
        {
            do
            {
                if (!micro_step())
                {
                    print_finish();
                    return;
                }
            } while (unit.microPc() != unit.beginAddress());
        }
        cout << BOLD << "executed " << n << (n == 1 ? " instruction" : " instructions") << RESET << "    " << DIM << "uPC back at "
             << target_text(unit.microPc()) << RESET << "\n";
    }

    // run: to the end, silently
    // void cmd_run()
    // {
    //     if (!unit.active())
    //     {
    //         not_running();
    //         return;
    //     }
    //     while (unit.stepMicro()) {}
    //     print_finish();
    // }

    void cmd_restart()
    {
        unit.begin(machine);
        current = -1;
        info("restarted - micro-PC at " + target_text(unit.microPc()));
        cmd_state();
    }

    void cmd_state()
    {
        cout << BOLD << unit.name() << " micro control unit" << RESET << "  (" << unit.wordBits() << "-bit words, " << unit.wordsUsed()
             << " control-memory words, " << unit.fetchStages() << "-stage micro-sequencer)\n";
        if (unit.active())
        {
            const int upc = unit.microPc();
            const MicroInstr &m = unit.microInstrAt(upc);
            cout << "  uPC            " << BOLD << target_text(upc) << RESET << "\n";
            cout << "  next uinstr    " << CYAN << toText(m) << RESET << "   (fetch mux: " << fetch_of(m.op) << ")\n";
            if (current >= 0)
                cout << "  instruction    pc=" << current << "  " << text_of(current) << "\n";
            else
                cout << "  instruction    " << DIM << "none fetched yet" << RESET << "\n";
        }
        else
        {
            cout << "  uPC            " << DIM << "run finished" << RESET << "\n";
        }
        cout << "  executed       " << unit.instructionsExecuted() << " instructions, " << unit.microInstructionsExecuted() << " micro-instructions, "
             << unit.cycles() << " clocks\n";
        cout << "  flags          Z=" << machine.zero << " N=" << machine.neg << " O=" << machine.over << " C=" << machine.carry << "\n";
    }

    void cmd_uregs()
    {
        const MicroDatapath *dp = unit.datapath();
        if (!dp)
        {
            fail("no run yet - use 'restart'");
            return;
        }
        for (int i = 0; i < kNumMRegs; i++)
        {
            const int32_t v = (int32_t)dp->reg(static_cast<MReg>(i));
            cout << (v != 0 ? BOLD : DIM) << left << setw(14) << mregName(static_cast<MReg>(i)) << "= " << right << setw(11) << v << RESET;
            cout << (i % 3 == 2 ? "\n" : "   ");
        }
        if (kNumMRegs % 3 != 0)
            cout << "\n";
    }

    // void cmd_registers()
    // {
    //     for (size_t i = 0; i < machine.Reg.size(); i++)
    //     {
    //         cout << (machine.Reg[i] != 0 ? BOLD : DIM) << left << setw(6) << reg_name((int)i) << "= " << right << setw(11) << machine.Reg[i] << RESET;
    //         cout << (i % 4 == 3 ? "\n" : "   ");
    //     }
    //     cout << "pc = " << machine.pc << "    flags: Z=" << machine.zero << " N=" << machine.neg << " O=" << machine.over << " C=" << machine.carry << "\n";
    // }

    // void cmd_memory()
    // {
    //     int used = 0;
    //     for (size_t i = 0; i < machine.memory.size(); i++)
    //         if (machine.memory[i] != 0)
    //         {
    //             cout << "mem[" << i << "] = " << machine.memory[i] << "\n";
    //             used++;
    //         }
    //     if (used == 0)
    //         cout << DIM << "no memory location holds data" << RESET << "\n";
    //     else
    //         cout << DIM << used << " of " << machine.memory.size() << " locations in use" << RESET << "\n";
    // }

    // void cmd_stack()
    // {
    //     StackMemoryVisualizer visualizer(machine);
    //     visualizer.render(std::cout, 16);
    // }

    // The control store; the micro-PC is marked with "=>".
    // void cmd_listing()
    // {
    //     istringstream in(unit.listing());
    //     string line;
    //     bool header = true;
    //     while (getline(in, line))
    //     {
    //         if (header)
    //         {
    //             cout << BOLD << line << RESET << "\n";
    //             header = false;
    //             continue;
    //         }
    //         const bool here = unit.active() && line.compare(0, 4, addr4(unit.microPc())) == 0;
    //         cout << (here ? YELLOW + string("=> ") + BOLD : string("   ")) << line << (here ? RESET : "") << "\n";
    //     }
    // }

    void cmd_words()
    {
        showWords = !showWords;
        info(string("control word display ") + (showWords ? "on" : "off"));
    }

    void cmd_help()
    {
        cout << BOLD << "Micro debug commands (" << unit.name() << " control unit)" << RESET << "\n";
        cout << "  " << CYAN << "ustep [n]" << RESET << "     execute n micro-instructions (default 1) and show the micro-PC for each\n";
        cout << "  " << CYAN << "step [n]" << RESET << "      execute n whole instructions (all their micro-instructions)\n";
        // cout << "  " << CYAN << "run" << RESET << "           run to the end of the program\n";
        cout << "  " << CYAN << "restart" << RESET << "       reset the machine, micro-PC back to the start\n";
        cout << "  " << CYAN << "state" << RESET << "         micro-PC, next micro-instruction, current instruction, counters\n";
        cout << "  " << CYAN << "uregs" << RESET << "         show the micro-registers\n";
        // cout << "  " << CYAN << "registers" << RESET << "     show all registers and flags\n";
        // cout << "  " << CYAN << "memory" << RESET << "        show every memory location that holds data (non-zero)\n";
        // cout << "  " << CYAN << "stack" << RESET << "         display stack memory frame\n";
        // cout << "  " << CYAN << "listing" << RESET << "       control-memory listing, the micro-PC is marked\n";
        cout << "  " << CYAN << "words" << RESET << "         show / hide the encoded control word of every micro-instruction\n";
        cout << "  " << CYAN << "clear" << RESET << "         clear the terminal\n";
        cout << "  " << CYAN << "help" << RESET << "          show this list\n";
        cout << "  " << CYAN << "exit" << RESET << "          leave micro debug mode\n";
    }

    bool parse_count(const string &arg, int &n)
    {
        if (arg.empty())
        {
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
            fail("the count must be at least 1");
            return false;
        }
        return true;
    }

public:
    PekkaMicroDebugger(MachineState &m, MicroControlUnit &u) : machine(m), unit(u) {}

    void run()
    {
        unit.begin(machine);
        load_source_text();

        int instructions = 0;
        for (const string &word : machine.instrMemory)
            if (!word.empty() && word[0] != '.')
                instructions++;

        ok("micro debug mode (" + string(unit.name()) + " control unit, " + to_string(instructions) + " instructions) - 'help' lists the commands");
        cmd_state();

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

            const bool takes_arg = (cmd == "ustep" || cmd == "step");
            if (!arg.empty() && !takes_arg)
            {
                fail("'" + cmd + "' takes no arguments");
                continue;
            }

            int n = 1;
            try
            {
                if (cmd == "ustep")
                {
                    if (parse_count(arg, n))
                        cmd_ustep(n);
                }
                else if (cmd == "step")
                {
                    if (parse_count(arg, n))
                        cmd_step(n);
                }
                // else if (cmd == "run")
                //     cmd_run();
                else if (cmd == "restart")
                    cmd_restart();
                else if (cmd == "state")
                    cmd_state();
                else if (cmd == "uregs")
                    cmd_uregs();
                // else if (cmd == "registers")
                //     cmd_registers();
                // else if (cmd == "memory")
                //     cmd_memory();
                // else if (cmd == "stack")
                //     cmd_stack();
                // else if (cmd == "listing")
                //     cmd_listing();
                else if (cmd == "words")
                    cmd_words();
                else if (cmd == "clear")
                    clear_terminal();
                else if (cmd == "help")
                    cmd_help();
                else if (cmd == "exit")
                    return;
                else
                    fail("command not found: " + cmd + "  (type 'help')");
            }
            catch (const exception &e)
            {
                fail(string("micro engine error: ") + e.what());
            }
        }
    }
};