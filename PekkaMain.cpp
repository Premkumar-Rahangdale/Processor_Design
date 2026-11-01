#ifdef _WIN32
// must come before "using namespace std" (isa.hpp) to avoid the Windows "byte" clash
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "include/isa.hpp"
#include "include/MachineState.hpp"
#include "src/assembler.cpp"
#include "src/disassembler.cpp"
#include "include/pipeline6stage.hpp"
#include "src/controlunit.cpp"
#include "src/alu.cpp"
#include "src/pipeline6stage.cpp"
#include "src/loader.cpp"
#include "src/debugger.cpp"
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
