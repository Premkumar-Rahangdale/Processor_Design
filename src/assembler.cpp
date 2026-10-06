#include "../include/isa.hpp"
#include "../include/MachineState.hpp"
#include <fstream>
#include <sstream>

using namespace std;
class Assembler
{
public:
    void assemble(MachineState & machine){
        first_pass(machine);
        second_pass(machine);
    }
    vector<string> tokenize(string &line)
    {
        stringstream ss(line);
        vector<string> tokens;
        string token;
        bool transform_baseoffset = false;
        while (ss >> token)
        {
            tokens.push_back(token);
            if(tokens.size()==1 && (token == "ld" || token == "st")){
                transform_baseoffset = true;
            }
        }
        if(transform_baseoffset){
            string baseoff = tokens.back();
            tokens.pop_back();
            string offset="";
            string base="";
            bool sw=false;
            for(auto it:baseoff){
                if(it==']'){break;}
                if(it=='['){sw=true;continue;}
                if(!sw){
                    offset+=it;
                }
                else{
                    base+=it;
                }
            }
            tokens.push_back(base);
            tokens.push_back(offset);
        }
        return tokens;
    }

    string reg_decode(string &reg)
    {
        if(reg == "sp") {
            string act = "r2";
            return reg_decode(act);
        }
        if(reg == "ra") {
            string act = "r1";
            return reg_decode(act);
        }
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

    void first_pass(MachineState& machine)
    {
        ifstream input("tests/t_asmblr.txt");
        string ins;
        int instr_count = 0;
        while (getline(input, ins))
        {
            if (ins[0] == '.')
            {
                ins.pop_back();
                machine.labelAddress[ins] = instr_count + 1;
                machine.inverseLabelAddress[instr_count + 1] = ins;
            }
            instr_count++;
        }
        input.close();
    }

    void second_pass(MachineState &machine)
    {
        ifstream in("tests/t_asmblr.txt");
        ofstream out("tests/o_asmblr.txt");
        string ins;
        int instr_count = 0;
        while (getline(in, ins))
        {
            string output;
            vector<string> tokens = tokenize(ins);
            int branch_instr = isa::branch[tokens[0]];
            if (ins[0] == '.'){
                instr_count++;
                continue;
            }
            output += isa::opcode[tokens[0]];
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
                int offset = machine.labelAddress[tokens[1]] - instr_count;
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
            machine.instrMemory.push_back(output);
            instr_count++;
            out << output << "\n";
            out.flush();
        };
        in.close();
        out.close();
    }
};

