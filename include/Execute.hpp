#pragma once

#include "MachineState.hpp"
#include "isa.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
using namespace std;

class CPUExecution {
private:
    static int32_t signExtend(uint32_t value, unsigned bits) {
        const uint32_t sign = uint32_t{1} << (bits - 1);
        return static_cast<int32_t>((value ^ sign) - sign);
    }

    static void setResultFlags(MachineState& machine, int32_t result) {
        machine.zero = (result == 0);
        machine.neg = (result < 0);
    }

    static int32_t readRegister(const MachineState& machine, uint32_t index) {
        if (index >= machine.Reg.size()) throw runtime_error("Invalid register index");
        return index == 0 ? 0 : machine.Reg[index];
    }

    static void writeRegister(MachineState& machine, uint32_t index, int32_t value) {
        if (index >= machine.Reg.size()) throw runtime_error("Invalid register index");
        if (index != 0) machine.Reg[index] = value;
        machine.Reg[0] = 0;
    }

    static uint32_t checkedAddress(int64_t address, const MachineState& machine) {
        if (address < 0 || address >= static_cast<int64_t>(machine.memory.size())) {
            throw runtime_error("Memory address out of range: " + to_string(address));
        }
        return static_cast<uint32_t>(address);
    }

public:
    bool finished(const MachineState& machine) const {
        return machine.pc >= 0 && static_cast<size_t>(machine.pc) >= machine.instrMemory.size();
    }

    void executeInstruction(MachineState& machine) {
        if (machine.pc < 0 || static_cast<size_t>(machine.pc) >= machine.instrMemory.size()) {
            throw runtime_error("Program counter is outside instruction memory");
        }

        const int currentPC = machine.pc;
        // const string& encoded = machine.instrMemory[static_cast<size_t>(currentPC)];
        const string& encoded = machine.instrMemory[currentPC];
        if (encoded.size() != 32 || encoded.find_first_not_of("01") != string::npos) {
            throw runtime_error("Invalid 32-bit instruction at PC " + to_string(currentPC));
        }

        const uint32_t instruction = static_cast<uint32_t>(stoull(encoded, nullptr, 2));
        const uint32_t opcode = (instruction >> 27) & 0x1F;
        const bool immediate = ((instruction >> 26) & 1) != 0;
        const uint32_t rd = (instruction >> 21) & 0x1F;
        const uint32_t rs1 = (instruction >> 16) & 0x1F;
        const uint32_t rs2 = (instruction >> 11) & 0x1F;
        const int32_t imm = signExtend(instruction & 0xFFFF, 16);
        const int32_t operandA = readRegister(machine, rs1);
        const int32_t operandB = immediate ? imm : readRegister(machine, rs2);
        const int32_t moveOperand = immediate ? imm : readRegister(machine, rs2);

        machine.pc = currentPC + 1;
        machine.carry = false;
        machine.over = false;  
        try {
            switch (opcode) {
                case 1: { // add
                    const uint32_t resultBits = static_cast<uint32_t>(static_cast<uint32_t>(operandA) + static_cast<uint32_t>(operandB));
                    const int32_t result = static_cast<int32_t>(resultBits);
                    machine.carry = (static_cast<uint64_t>(static_cast<uint32_t>(operandA)) +
                                    static_cast<uint64_t>(static_cast<uint32_t>(operandB))) >
                                    numeric_limits<uint32_t>::max();
                    machine.over = ((~(operandA ^ operandB) & (operandA ^ result)) < 0);
                    writeRegister(machine, rd, result);
                    setResultFlags(machine, result);
                    break;
                }
                case 2: { // sub
                    const uint32_t resultBits = static_cast<uint32_t>(static_cast<uint32_t>(operandA) - static_cast<uint32_t>(operandB));
                    const int32_t result = static_cast<int32_t>(resultBits);
                    machine.carry = static_cast<uint32_t>(operandA) >= static_cast<uint32_t>(operandB);
                    machine.over = (((operandA ^ operandB) & (operandA ^ result)) < 0);
                    writeRegister(machine, rd, result);
                    setResultFlags(machine, result);
                    break;
                }
                case 3: { // mul
                    const int64_t wide = static_cast<int64_t>(operandA) * operandB;
                    const int32_t result = static_cast<int32_t>(static_cast<uint32_t>(wide));
                    machine.over = wide > numeric_limits<int32_t>::max() || wide < numeric_limits<int32_t>::min();
                    writeRegister(machine, rd, result);
                    setResultFlags(machine, result);
                    break;
                }
                case 4: // div
                case 5: { // mod
                    if (operandB == 0) throw runtime_error("Division by zero at PC " + to_string(currentPC));
                    if (operandA == numeric_limits<int32_t>::min() && operandB == -1) {
                        if (opcode == 4) throw runtime_error("Signed division overflow at PC " + to_string(currentPC));
                        writeRegister(machine, rd, 0);
                        setResultFlags(machine, 0);
                    } else {
                        const int32_t result = opcode == 4 ? operandA / operandB : operandA % operandB;
                        writeRegister(machine, rd, result);
                        setResultFlags(machine, result);
                    }
                    break;
                }
                case 6: { // cmp uses rs1 and operand 2; rd field is unused
                    const int64_t difference = static_cast<int64_t>(operandA) - operandB;
                    machine.zero = operandA == operandB;
                    machine.neg = operandA < operandB;
                    machine.over = difference > numeric_limits<int32_t>::max() || difference < numeric_limits<int32_t>::min();
                    machine.carry = static_cast<uint32_t>(operandA) >= static_cast<uint32_t>(operandB);
                    break;
                }
                case 7: { const int32_t result = operandA & operandB; writeRegister(machine, rd, result); setResultFlags(machine, result); break; }
                case 8: { const int32_t result = operandA | operandB; writeRegister(machine, rd, result); setResultFlags(machine, result); break; }
                case 9: { // not uses rd and the second operand field
                    const int32_t result = ~moveOperand;
                    writeRegister(machine, rd, result);
                    setResultFlags(machine, result);
                    break;
                }
                case 10: { const int32_t result = operandA ^ operandB; writeRegister(machine, rd, result); setResultFlags(machine, result); break; }
                case 11: { writeRegister(machine, rd, moveOperand); setResultFlags(machine, moveOperand); break; } // mov
                case 12: { // movu: move unsigned 16-bit immediate
                    const int32_t result = immediate ? static_cast<int32_t>(instruction & 0xFFFFu) : moveOperand;
                    writeRegister(machine, rd, result); setResultFlags(machine, result); break;
                }
                case 13: { // movh: place the immediate in the upper 16 bits
                    const int32_t result = immediate ? static_cast<int32_t>((instruction & 0xFFFFu) << 16) : moveOperand;
                    writeRegister(machine, rd, result); setResultFlags(machine, result); break;
                }
                case 14: // lsl
                case 15: // lsr
                case 16: { // asr
                    const int32_t shift = immediate ? static_cast<int32_t>(instruction & 0xFFFFu) : readRegister(machine, rs2);
                    if (shift < 0 || shift > 31) throw runtime_error("Shift amount must be between 0 and 31 at PC " + to_string(currentPC));
                    uint32_t bits = static_cast<uint32_t>(operandA);
                    int32_t result;
                    if (opcode == 14) result = static_cast<int32_t>(bits << shift);
                    else if (opcode == 15) result = static_cast<int32_t>(bits >> shift);
                    else result = static_cast<int32_t>(operandA >> shift);
                    writeRegister(machine, rd, result); setResultFlags(machine, result); break;
                }
                case 17: // nop
                    break;
                case 18: // ld: rd <- memory[rs1 + sign-extended offset]
                case 19: { // st: memory[rs1 + offset] <- rd
                    const int32_t offset = signExtend(instruction & 0xFFFFu, 16);
                    const uint32_t address = checkedAddress(static_cast<int64_t>(readRegister(machine, rs1)) + offset, machine);
                    if (opcode == 18) {
                        const int32_t value = machine.memory[address];
                        writeRegister(machine, rd, value);
                        setResultFlags(machine, value);
                    } else {
                        machine.memory[address] = readRegister(machine, rd);
                    }
                    break;
                }
                case 20: // beq
                case 21: // bgt
                case 22: // bsm
                case 23: // b
                case 24: { // call
                    const int32_t offset = signExtend(instruction & 0x07FFFFFFu, 27);
                    bool take = opcode == 23 || opcode == 24;
                    if (opcode == 20) take = machine.zero;
                    if (opcode == 21) take = !machine.zero && !machine.neg;
                    if (opcode == 22) take = machine.neg;
                    if (opcode == 24) writeRegister(machine, 1, currentPC + 1);
                    if (take) {
                        const int64_t target = static_cast<int64_t>(currentPC) + offset;
                        if (target < 0 || target > static_cast<int64_t>(machine.instrMemory.size())) {
                            throw runtime_error("Branch target out of range: " + to_string(target));
                        }
                        machine.pc = static_cast<int>(target);
                    }
                    break;
                }
                case 25: // ret
                    if (machine.Reg[1] < 0 || static_cast<size_t>(machine.Reg[1]) > machine.instrMemory.size()) {
                        throw runtime_error("Return address out of range: " + to_string(machine.Reg[1]));
                    }
                    machine.pc = machine.Reg[1];
                    break;
                default:
                    throw runtime_error("Unknown opcode " + to_string(opcode) + " at PC " + to_string(currentPC));
            }
        } catch (...) {
            machine.pc = currentPC;
            throw;
        }

        machine.Reg[0] = 0;
    }
};
