#pragma once
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

class Loader
{
public:
    string clean_line(const string &line)
    {
        string code = line;
        size_t cut = code.find("//");
        if (cut != string::npos)
            code.erase(cut);

        string out;
        bool pending_space = false;
        for (unsigned char ch : code)
        {
            if (isspace(ch))
            {
                pending_space = !out.empty();
                continue;
            }
            if (pending_space)
            {
                out += ' ';
                pending_space = false;
            }
            out += (char)ch;
        }
        return out;
    }

    void load()
    {
        ifstream in("tests/assembly_program.txt");

        vector<string> program;
        string line;
        bool first_line = true;
        while (getline(in, line))
        {
            // skip the UTF-8 BOM some editors put at the start of a file
            if (first_line && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
                line.erase(0, 3);
            first_line = false;

            string clean = clean_line(line);
            if (!clean.empty())
                program.push_back(clean);
        }
        in.close();

        ofstream out("tests/t_asmblr.txt");

        for (const string &ins : program)
            out << ins << "\n";
        out.close();
    }
};
