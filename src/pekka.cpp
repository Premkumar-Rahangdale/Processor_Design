#pragma once
#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace std;

// P.E.K.K.A. - terminal style command line interface of the RISC201 processor.
//
// It only drives the parts that already exist:
//   Loader, Assembler, Disassembler, Pipeline4stage / Pipeline6stage, the hardwired / horizontal /
//   vertical control units, PekkaDebugger (and MachineState).
// Include this file after those (see PekkaMain.cpp).
//
// Commands:  start | load | assemble | disassemble | debug | execute | help | exit
// Options:   config
// 'start' asks for the number of pipeline stages (4|6) and the control unit (hardwired|horizontal|vertical)
// Fixed:     data hazards use forwarding and branches use the 2-bit predictor (not selectable)
class Pekka
{
private:
    MachineState machine;
    Assembler assembler;
    Disassembler disassembler;
    Loader loader;

    bool started = false;    // processor powered on
    bool loaded = false;     // tests/t_asmblr.txt holds a cleaned program
    bool assembled = false;  // machine.instrMemory + tests/o_asmblr.txt are ready

    // processor configuration (used by 'execute' and 'debug')
    int stages = 6;                 // 4 or 6 pipeline stages
    int control = 0;                // 0 hardwired, 1 horizontal microprogram, 2 vertical microprogram
    const bool forwarding = true;   // data hazards: results are always forwarded
    const bool predictor = true;    // branch prediction: always the 2-bit predictor + BTB
    HorizontalMicroControlUnit horizontal;
    VerticalMicroControlUnit vertical;

    // ANSI colours (the terminal must understand them, see PekkaMain.cpp)
    const string RESET = "\033[0m";
    const string BOLD = "\033[1m";
    const string DIM = "\033[2m";
    const string RED = "\033[31m";
    const string GREEN = "\033[32m";
    const string YELLOW = "\033[33m";
    const string BLUE = "\033[34m";
    const string CYAN = "\033[36m";

    void ok(const string &msg) { cout << GREEN << "[ OK ] " << RESET << msg << "\n"; }
    void fail(const string &msg) { cout << RED << "[FAIL] " << RESET << msg << "\n"; }
    void warn(const string &msg) { cout << YELLOW << "[WARN] " << RESET << msg << "\n"; }
    void info(const string &msg) { cout << CYAN << "[INFO] " << RESET << msg << "\n"; }

    void clear_screen() { cout << "\033[2J\033[H"; }

public:
    Pekka()
    {
        const MicroProgram program = buildRisc201Microprogram();   // the control memory contents
        horizontal.load(program);
        vertical.load(program);
    }

private:
    void banner()
    {
        cout << CYAN << BOLD;
        cout << R"( ____  _____ _  __ _  __    _    )" << "\n";
        cout << R"(|  _ \| ____| |/ /| |/ /   / \   )" << "\n";
        cout << R"(| |_) |  _| | ' / | ' /   / _ \  )" << "\n";
        cout << R"(|  __/| |___| . \ | . \  / ___ \ )" << "\n";
        cout << R"(|_|   |_____|_|\_\|_|\_\/_/   \_\)" << "\n";
        cout << RESET;
        cout << DIM << "RISC201 processor terminal" << RESET << "\n";
        cout << DIM << "----------------------------------------" << RESET << "\n";
        cout << "type " << BOLD << "start" << RESET << " to power on the processor, "
             << BOLD << "help" << RESET << " for all commands\n\n";
    }

    void prompt()
    {
        cout << GREEN << BOLD << "pekka@risc201" << RESET << ":" << BLUE << BOLD << "~" << RESET << "$ " << flush;
    }

    static string normalize(const string &line)
    {
        size_t start = 0, end = line.size();
        while (start < end && isspace((unsigned char)line[start]))
            start++;
        while (end > start && isspace((unsigned char)line[end - 1]))
            end--;
        string cmd = line.substr(start, end - start);
        transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return (char)tolower(c); });
        return cmd;
    }

    bool need_started()
    {
        if (!started)
        {
            fail("processor is off - type 'start' first");
            return false;
        }
        return true;
    }

    void forget_program()
    {
        machine.instrMemory.clear();
        machine.labelAddress.clear();
        machine.inverseLabelAddress.clear();
        assembled = false;
    }

    // ------------------------------------------------------------ configuration

    string control_name() const
    {
        return control == 1 ? "horizontal microprogrammed" : control == 2 ? "vertical microprogrammed" : "hardwired";
    }

    // The pipeline selected by the options. The micro control units are owned by Pekka.
    unique_ptr<Pipeline> make_pipeline()
    {
        unique_ptr<Pipeline> p;
        if (stages == 4)
            p = make_unique<Pipeline4stage>();
        else
            p = make_unique<Pipeline6stage>();
        p->forwarding = forwarding;
        p->predictor = predictor ? Predictor::TWO_BIT : Predictor::NOT_TAKEN;
        p->useMicrocode(control == 1 ? static_cast<const MicroControlUnit *>(&horizontal)
                        : control == 2 ? static_cast<const MicroControlUnit *>(&vertical)
                                       : nullptr);
        return p;
    }

    string config_text() const
    {
        return to_string(stages) + "-stage pipeline, " + control_name() + " control, " +
               (forwarding ? "forwarding" : "stalls only") + ", " +
               (predictor ? "2-bit branch prediction" : "predict-not-taken");
    }

    void cmd_config()
    {
        cout << BOLD << "Processor configuration" << RESET << "\n";
        cout << "  pipeline    " << CYAN << stages << "-stage" << RESET << (stages == 4 ? "  (IF ID EX/MEM WB)" : "  (IF ID RR EX MEM WB)") << "\n";
        cout << "  control     " << CYAN << control_name() << RESET << "\n";
        cout << "  hazards     " << CYAN << (forwarding ? "forwarding" : "stalls only") << RESET << "\n";
        cout << "  branches    " << CYAN << (predictor ? "2-bit predictor with branch target buffer" : "predict not taken") << RESET << "\n";
    }

    // Asks a question until the answer is one of `choices`. Returns the index of the choice, -1 on end of input.
    int ask_choice(const string &question, const vector<string> &choices)
    {
        while (true)
        {
            cout << BOLD << question << RESET << " ";
            for (size_t i = 0; i < choices.size(); i++)
                cout << (i ? " | " : "[") << choices[i];
            cout << "] " << flush;

            string line;
            if (!getline(cin, line))
            {
                cout << "\n";
                return -1;
            }
            string answer = normalize(line);
            for (size_t i = 0; i < choices.size(); i++)
                if (answer == choices[i])
                    return (int)i;
            fail("please enter one of the listed options");
        }
    }

    void cmd_help()
    {
        cout << BOLD << "Commands" << RESET << "\n";
        cout << "  " << CYAN << "start" << RESET << "         power on the processor\n";
        cout << "  " << CYAN << "load" << RESET << "          load the assembly program (comments and extra spaces removed)\n";
        cout << "  " << CYAN << "assemble" << RESET << "      assemble the loaded program into machine code\n";
        cout << "  " << CYAN << "disassemble" << RESET << "   disassemble the machine code back into assembly\n";
        cout << "  " << CYAN << "debug" << RESET << "         enter debug mode\n";
        cout << "  " << CYAN << "execute" << RESET << "       run the whole program on the configured pipeline and show the statistics\n";
        cout << "  " << CYAN << "config" << RESET << "        show the processor configuration\n";
        cout << "  " << CYAN << "help" << RESET << "          show this list\n";
        cout << "  " << CYAN << "exit" << RESET << "          shut down and leave P.E.K.K.A.\n";
    }

    void cmd_start()
    {
        if (started)
        {
            warn("processor is already running");
            return;
        }

        // the processor is configured before it is powered on
        int stageChoice = ask_choice("pipeline stages?", {"4", "6"});
        if (stageChoice < 0)
            return;
        int controlChoice = ask_choice("control unit?", {"hardwired", "horizontal", "vertical"});
        if (controlChoice < 0)
            return;
        stages = stageChoice == 0 ? 4 : 6;
        control = controlChoice;

        machine.reset();   // registers, data memory, flags, pc
        forget_program();
        started = true;
        loaded = false;
        ok("processor started (" + to_string(machine.Reg.size()) + " registers, " +
           to_string(machine.memory.size()) + " words of data memory)");
    }

    void cmd_load()
    {
        if (!need_started())
            return;

        // Loader::load() does not report a missing source file, so check first.
        ifstream source("tests/assembly_program.txt");
        if (!source)
        {
            fail("cannot open tests/assembly_program.txt");
            return;
        }
        source.close();

        loader.load();
        forget_program();

        ifstream cleaned("tests/t_asmblr.txt");
        string line;
        int lines = 0;
        while (getline(cleaned, line))
            if (!line.empty())
                lines++;

        if (lines == 0)
        {
            loaded = false;
            fail("tests/assembly_program.txt has no instructions");
            return;
        }
        loaded = true;
        ok("program loaded (" + to_string(lines) + " lines)");
    }

    void cmd_assemble()
    {
        if (!need_started())
            return;
        if (!loaded)
        {
            fail("no program loaded - use 'load' first");
            return;
        }

        forget_program();   // the assembler appends to instrMemory, so start clean
        try
        {
            assembler.assemble(machine);
        }
        catch (const exception &e)
        {
            forget_program();
            fail(string("assembly failed: ") + e.what());
            return;
        }
        catch (...)
        {
            forget_program();
            fail("assembly failed");
            return;
        }

        int instructions = 0;
        for (const string &word : machine.instrMemory)
            if (!word.empty() && word[0] != '.')
                instructions++;

        if (instructions == 0)
        {
            forget_program();
            fail("nothing was assembled");
            return;
        }
        assembled = true;
        ok("assembled " + to_string(instructions) + " instructions");
    }

    void cmd_disassemble()
    {
        if (!need_started())
            return;
        if (!assembled)
        {
            fail("no machine code - use 'assemble' first");
            return;
        }
        try
        {
            disassembler.disassembler(machine);
        }
        catch (const exception &e)
        {
            fail(string("disassembly failed: ") + e.what());
            return;
        }
        catch (...)
        {
            fail("disassembly failed");
            return;
        }
        ok("disassembly complete");
    }

    void cmd_debug()
    {
        if (!need_started())
            return;
        if (!assembled)
        {
            fail("no machine code - use 'assemble' first");
            return;
        }
        unique_ptr<Pipeline> pipeline = make_pipeline();
        info("debugging on the " + config_text());
        PekkaDebugger debugger(machine, *pipeline);
        debugger.run();
        info("left debug mode");
    }

    void cmd_execute()
    {
        if (!need_started())
            return;
        if (!assembled)
        {
            fail("no machine code - use 'assemble' first");
            return;
        }

        info("executing on the " + config_text());
        unique_ptr<Pipeline> pipeline = make_pipeline();
        try
        {
            pipeline->run(machine);
        }
        catch (const exception &e)
        {
            fail(string("execution failed: ") + e.what());
            return;
        }
        catch (...)
        {
            fail("execution failed");
            return;
        }
        if (pipeline->faulted())
            fail("stopped by an exception (cause " + to_string(pipeline->faultCause()) + ", R4 = cause, R5 = index of the faulting instruction)");
        else
            ok("execution finished");
        print_statistics(*pipeline);

        if (control != 0)
            run_micro_engine();
    }

    // With a microprogrammed control unit the microprogram is also run on its own datapath
    // (MicroControlUnit::run: fetch / decode / execute as micro-operations, no pipeline). That shows the
    // cost of executing the program micro-instruction by micro-instruction, and the final state is
    // compared with the pipeline's to check that the control memory and the pipeline agree.
    void run_micro_engine()
    {
        MicroControlUnit &unit = control == 1 ? static_cast<MicroControlUnit &>(horizontal)
                                              : static_cast<MicroControlUnit &>(vertical);
        MachineState micro = machine;   // keeps instrMemory and labels; run() resets the rest
        try
        {
            unit.run(micro);
        }
        catch (const exception &e)
        {
            fail(string("micro engine failed: ") + e.what());
            return;
        }

        cout << "\n";
        info("microprogram run on the " + string(unit.name()) + " control unit (" + to_string(unit.wordBits()) + "-bit words, " +
             to_string(unit.wordsUsed()) + " control-memory words)");
        if (unit.hitInstructionLimit())
            warn("stopped at the instruction limit");
        else if (unit.faulted())
            fail("stopped by an exception (cause " + to_string(unit.faultCause()) + ")");
        else
            ok("program finished");

        long instr = unit.instructionsExecuted();
        cout << "  instructions " << BOLD << instr << RESET << "    micro-instructions " << BOLD << unit.microInstructionsExecuted() << RESET
             << "    clocks " << BOLD << unit.cycles() << RESET << "    CPI " << BOLD << fixed << setprecision(2)
             << (instr ? double(unit.cycles()) / double(instr) : 0.0) << RESET << "\n";
        cout << defaultfloat;

        // compare the final state (r2 is left out: only the pipeline initialises the stack pointer)
        bool same = true;
        for (size_t i = 0; i < machine.Reg.size(); i++)
            if (i != 2 && machine.Reg[i] != micro.Reg[i])
                same = false;
        if (machine.memory != micro.memory)
            same = false;
        if (machine.zero != micro.zero || machine.neg != micro.neg || machine.over != micro.over || machine.carry != micro.carry)
            same = false;
        if (same)
            ok("final registers, memory and flags match the pipeline run");
        else
            warn("final state differs from the pipeline run (stack instructions and exceptions are handled differently by the two engines)");
    }

    void print_statistics(const Pipeline &p)
    {
        cout << "  instructions " << BOLD << p.instructionsExecuted() << RESET << "    clocks " << BOLD << p.cycles() << RESET
             << "    CPI " << BOLD << fixed << setprecision(2) << p.cpi() << RESET << "\n";
        cout << "  data-hazard stalls " << p.dataStallCycles() << "    branches " << p.branches() << "    mispredicted " << p.mispredictions();
        if (p.branches() > 0)
            cout << " (accuracy " << setprecision(1) << 100.0 * p.predictionAccuracy() << "%)";
        cout << "    squashed " << p.squashedInstructions() << "    control-path bubbles " << p.controlBubbles() << "\n";
        cout << defaultfloat;
    }

public:
    void run()
    {
        clear_screen();
        banner();

        string line;
        while (true)
        {
            prompt();
            if (!getline(cin, line))
            {
                cout << "\n";
                break;
            }

            string cmd = normalize(line);
            if (cmd.empty())
                continue;

            string arg, extra;
            {
                istringstream words(cmd);
                words >> cmd >> arg >> extra;
            }
            if (!extra.empty() || !arg.empty())
            {
                fail("'" + cmd + "' takes no arguments  (type 'help')");
                continue;
            }

            if (cmd == "start")
                cmd_start();
            else if (cmd == "load")
                cmd_load();
            else if (cmd == "assemble")
                cmd_assemble();
            else if (cmd == "disassemble")
                cmd_disassemble();
            else if (cmd == "debug")
                cmd_debug();
            else if (cmd == "execute")
                cmd_execute();
            else if (cmd == "config")
                cmd_config();
            else if (cmd == "help")
                cmd_help();
            else if (cmd == "exit")
                break;
            else
                fail("command not found: " + cmd + "  (type 'help')");
        }

        cout << DIM << "P.E.K.K.A. shut down." << RESET << "\n";
    }
};