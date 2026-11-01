#pragma once
#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

using namespace std;

// P.E.K.K.A. - terminal style command line interface of the RISC201 processor.
//
// It only drives the parts that already exist:
//   Loader, Assembler, Disassembler, Pipeline6stage, PekkaDebugger (and MachineState).
// Include this file after those (see PekkaMain.cpp).
//
// Commands:  start | load | assemble | disassemble | debug | execute | help | exit
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

    // ---------------------------------------------------------------- commands

    void cmd_help()
    {
        cout << BOLD << "Commands" << RESET << "\n";
        cout << "  " << CYAN << "start" << RESET << "         power on the processor\n";
        cout << "  " << CYAN << "load" << RESET << "          load the assembly program (comments and extra spaces removed)\n";
        cout << "  " << CYAN << "assemble" << RESET << "      assemble the loaded program into machine code\n";
        cout << "  " << CYAN << "disassemble" << RESET << "   disassemble the machine code back into assembly\n";
        cout << "  " << CYAN << "debug" << RESET << "         enter debug mode\n";
        cout << "  " << CYAN << "execute" << RESET << "       run the whole program on the 6-stage pipeline\n";
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
        PekkaDebugger debugger(machine);
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

        info("executing on the 6-stage pipeline");
        try
        {
            Pipeline6stage pipeline;
            pipeline.run(machine);
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
        ok("execution finished");
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
