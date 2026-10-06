#include "../include/isa.hpp"
#include "../include/MachineState.hpp"
#include <fstream>
#include <sstream>

#include <map>

class Disassembler{
public:
    void disassembler(MachineState & machine){
        disassemble(machine);
    }

    uint32_t extractOpcode(uint32_t instruction) {
        return (instruction >> 27) & 0x1F;
    }

    uint32_t extractImmediate(uint32_t instruction) {
        return (instruction >> 26) & 1;
    }

    uint32_t extractRd(uint32_t instruction) {
        return (instruction >> 21) & 0x1F;
    }

    uint32_t extractRS1(uint32_t instruction) {
        return (instruction >> 16) & 0x1F;
    }

    uint32_t extractRS2(uint32_t instruction) {
        return (instruction >> 11) & 0x1F;
    }

    int signExtend(uint32_t v, int bits) {
        uint32_t m = 1u << (bits - 1);
        return (int)((v ^ m) - m);
    }

    int extractImm(uint32_t x) { 
        return signExtend(x & 0xFFFF, 16); 
    }

    int extractOffset(uint32_t x) { 
        return signExtend(x & 0x7FFFFFF, 27); 
    }

    void disassemble(MachineState &machine) {
        ifstream in("tests/o_asmblr.txt");
        ofstream out("tests/o_disasmblr.txt");

        string ins;
        int instr_count = 0;
        while (getline(in, ins)){
        if(machine.inverseLabelAddress.find(instr_count + 1) != machine.inverseLabelAddress.end()){
            out << machine.inverseLabelAddress[instr_count + 1] + ":"<<"\n";
            instr_count++;
        }
        string output; 
        uint32_t instruction = (uint32_t)stoul(ins, nullptr, 2);
        uint32_t opcode = extractOpcode(instruction);
        int i = extractImmediate(instruction);

        string rd = to_string(extractRd(instruction));
        string rs1 = to_string(extractRS1(instruction));
        string rs2;
        string imm;

        if (i)
            imm = to_string(extractImm(instruction));
        else
            rs2 = to_string(extractRS2(instruction));

        string op = isa::inverseOpcode[opcode];

        if (op == "nop" || op == "ret") {
            output = op;
        } else if (op == "b" || op == "call" || op == "beq" || op == "bgt" || op == "bsm"){
            output = op + " ";
            output += machine.inverseLabelAddress[instr_count + extractOffset(instruction)];
        } else if (op == "ld" || op == "st") {
            output = op;
            if(rd == "1") output +=" ra";
            else if(rd == "2") output +=" sp";
            else output += " r" + rd;

            output += " " + to_string(extractImm(instruction));

            if(rs1 == "1") output +="[ra]";
            else if(rs1 == "2") output +="[sp]";
            else output+= "[r" + rs1 + "]";

        } else if (op == "not" || op == "mov" || op == "movu" || op == "movh"){
            if (i){
                output = op;
                if(rd == "1") output +=" ra";
                else if(rd == "2") output +=" sp";
                else output += " r" + rd;
                output += " " + imm;
            }
            else{
                output = op;
                if(rd == "1") output +=" ra";
                else if(rd == "2") output +=" sp";
                else output += " r" + rd; 
                
                if(rs2 == "1") output +=" ra";
                else if(rs2 == "2") output +=" sp";
                else output += " r" + rs2;
            }
        } else if (op == "cmp") {
            if (i){
                output = op;
                if(rs1 == "1") output +=" ra";
                else if(rs1 == "2") output +=" sp";
                else output += " r" + rs1;
                output += " " + imm;
            }
            else{
                output = op;
                if(rs1 == "1") output +=" ra";
                else if(rs1 == "2") output +=" sp";
                else output +=" r" + rs1;

                if(rs2 == "1") output +=" ra";
                else if(rs2 == "2") output +=" sp";
                else output += " r" + rs2;
            }
        } else{
            if (i){
                output = op;
                if(rd == "1") output +=" ra";
                else if(rd == "2") output +=" sp";
                else output += " r" + rd;
                
                if(rs1 == "1") output +=" ra";
                else if(rs1 == "2") output +=" sp";
                else output += " r" + rs1;
                output += " " + imm;
            }
            else{
                output = op;
                if(rd == "1") output +=" ra";
                else if(rd == "2") output +=" sp";
                else output += " r" + rd;
                
                if(rs1 == "1") output +=" ra";
                else if(rs1 == "2") output +=" sp";
                else output += " r" + rs1;
                
                if(rs2 == "1") output +=" ra";
                else if(rs2 == "2") output +=" sp";
                else output += " r" + rs2;
            }
        }
        out << output << "\n";
        out.flush();
        instr_count++;
        };    
        in.close();
        out.close();
    }
};

