#ifdef _WIN32
// must come before "using namespace std" (isa.hpp) to avoid the Windows "byte" clash
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdlib>
#include <iostream>

// Wipes the terminal like cmd's "cls": the screen AND the scrollback, so the old output
// cannot be scrolled back to. Used by the 'clear' command of P.E.K.K.A. and of the debugger.
static void clear_terminal()
{
    std::cout << std::flush;
#ifdef _WIN32
    std::system("cls");
#else
    std::cout << "\033[3J\033[2J\033[H" << std::flush;   // scrollback, screen, cursor home
#endif
}

#include "include/isa.hpp"
#include "include/MachineState.hpp"
#include "include/stack_preprocessor.hpp"
#include "include/stack_safety.hpp"
#include "include/stack_visualizer.hpp"
#include "src/assembler.cpp"
#include "src/disassembler.cpp"
#include "include/pipeline6stage.hpp"
#include "src/controlunit.cpp"
#include "src/alu.cpp"
#include "src/pipeline.cpp"
#include "src/pipeline4stage.cpp"
#include "src/pipeline6stage.cpp"
#include "src/microinstr.cpp"
#include "src/microdatapath.cpp"
#include "src/microcode.cpp"
#include "src/microcontrolunit.cpp"
#include "src/loader.cpp"
#include "src/debugger.cpp"
#include "src/microdebugger.cpp"
#include "src/pekka.cpp"

using namespace std;

// Makes the Windows console understand colours / clear-screen codes and sets its title.
static void prepare_terminal()
{
#ifdef _WIN32
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode))
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleTitleA("P.E.K.K.A.");
#else
    cout << "\033]0;P.E.K.K.A.\007";
#endif
}

int main()
{
    prepare_terminal();
    Pekka pekka;
    pekka.run();
    return 0;
}