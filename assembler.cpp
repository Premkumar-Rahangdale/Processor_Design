#include <iostream>
#include <map>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
using namespace std;

map<string, string> opcode = {
    {"add", "00001"},
    {"sub", "00010"},
    {"mul", "00011"},
    {"div", "00100"},
    {"mod", "00101"},
    {"cmp", "00110"},
    {"and", "00111"},
    {"or", "01000"},
    {"not", "01001"},
    {"xor", "01010"},
    {"mov", "01011"},
    {"movu", "01100"},
    {"movh", "01101"},
    {"lsl", "01110"},
    {"lsr", "01111"},
    {"asr", "10000"},
    {"nop", "10001"},
    {"ld", "10010"},
    {"st", "10011"},
    {"beq", "10100"},
    {"bgt", "10101"},
    {"bsm", "10110"},
    {"b", "10111"},
    {"call", "11000"},
    {"ret", "11001"}};

map<string, int> branch = {
    {"add", 3},
    {"sub", 3},
    {"mul", 3},
    {"div", 3},
    {"mod", 3},
    {"cmp", 2},
    {"and", 3},
    {"or", 3},
    {"not", 2},
    {"xor", 3},
    {"mov", 2},
    {"movu", 2},
    {"movh", 2},
    {"lsl", 3},
    {"lsr", 3},
    {"asr", 3},
    {"nop", 0},
    {"ld", 3},
    {"st", 3},
    {"beq", 1},
    {"bgt", 1},
    {"bsm", 1},
    {"b", 1},
    {"call", 1},
    {"ret", 0}};

map<string, int> labels;

vector<string> tokenize(string &line)
{
    stringstream ss(line);
    vector<string> tokens;
    string token;
    while (ss >> token)
    {
        tokens.push_back(token);
    }

    return tokens;
}

string reg_decode(string &reg)
{
    string ans;
    if (reg.size() == 2)
    {
        int num = reg[1] - '0';
        for (int i = 4; i >= 0; i--)
        {
            ans += (char)('0' + ((num >> i) & 1));
        }
    }
    else if (reg.size() == 3)
    {
        int num = (reg[1] - '0') * 10 + reg[2] - '0';
        for (int i = 4; i >= 0; i--)
        {
            ans += (char)('0' + ((num >> i) & 1));
        }
    }
    return ans;
}

string imm_decode(string &imm, int size)
{
    int num = stoi(imm);
    string ans;
    for (int i = size - 1; i >= 0; i--)
    {
        ans += (char)(((num >> i) & 1) + '0');
    }
    return ans;
}

void first_pass()
{
    ifstream file("tests/t1_asmblr.txt");
    string ins;
    int instr_count = 0;
    while (getline(file, ins))
    {
        if (ins[0] == '.')
        {
            ins.pop_back();
            labels[ins] = instr_count + 1;
        }
        instr_count++;
    }
    file.close();
}

void second_pass()
{
    ifstream in("tests/t1_asmblr.txt");
    ofstream out("tests/o1_asmblr.txt");
    string ins;
    int instr_count = 0;
    while (getline(in, ins))
    {
        string output;
        vector<string> tokens = tokenize(ins);
        int branch_instr = branch[tokens[0]];
        if (ins[0] == '.')
            output = ins;
        else
        {
            output += opcode[tokens[0]];
            if (branch_instr == 3)
            {
                bool imm = 0;
                if (tokens[3][0] != 'r')
                {
                    output += '1';
                    imm = 1;
                }
                else
                    output += '0';
                output += reg_decode(tokens[1]);
                output += reg_decode(tokens[2]);
                if (imm)
                {
                    output += imm_decode(tokens[3], 16);
                }
                else
                {
                    output += reg_decode(tokens[3]);
                    for (int i = 0; i < 11; i++)
                        output += '0';
                }
            }
            else if (branch_instr == 2)
            {
                bool imm = 0;
                if (tokens[2][0] != 'r')
                {
                    output += '1';
                    imm = 1;
                }
                else
                    output += '0';
                if (tokens[0] == "cmp")
                {
                    for (int i = 0; i < 5; i++)
                        output += '0';
                }
                output += reg_decode(tokens[1]);
                if (tokens[0] != "cmp")
                {
                    for (int i = 0; i < 5; i++)
                        output += '0';
                }
                if (imm)
                {
                    output += imm_decode(tokens[2], 16);
                }
                else
                {
                    output += reg_decode(tokens[2]);
                    for (int i = 0; i < 11; i++)
                        output += '0';
                }
            }
            else if (branch_instr == 1)
            {
                int offset = labels[tokens[1]] - instr_count;
                for (int i = 26; i >= 0; i--)
                {
                    output += (char)(((offset >> i) & 1) + '0');
                }
            }
            else
            {
                for (int i = 0; i < 27; i++)
                    output += '0';
            }
        }
        instr_count++;
        out << output << "\n";
        out.flush();
    };
    in.close();
    out.close();
}
int main()
{
    first_pass();
    second_pass();
    return 0;
}