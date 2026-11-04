#pragma once

#include <stdexcept>
#include <string>
#include "MachineState.hpp"

enum class StackExceptionCause { STACK_OVERFLOW, STACK_UNDERFLOW, STACK_POINTER_CORRUPTION, STACK_ADDRESS_VIOLATION };

class StackRuntimeException : public std::runtime_error {
public:
    StackRuntimeException(StackExceptionCause cause, int address, const std::string& message)
        : std::runtime_error(message), cause_(cause), address_(address) {}
    StackExceptionCause cause() const noexcept { return cause_; }
    int address() const noexcept { return address_; }
private:
    StackExceptionCause cause_;
    int address_;
};

class StackExceptionFramework {
public:
    static constexpr int LOWER = 3072;
    static constexpr int UPPER = 4095;
    explicit StackExceptionFramework(MachineState& state) : machine_(state) {
        if (state.memory.size() <= UPPER) throw std::invalid_argument("memory is too small for configured stack region");
    }
    void initialize() { machine_.Reg[2] = UPPER + 1; }
    void checkPush() const {
        checkPointer();
        if (machine_.Reg[2] - 1 < LOWER) fail(StackExceptionCause::STACK_OVERFLOW, machine_.Reg[2] - 1, "stack overflow");
    }
    void checkPop() const {
        checkPointer();
        if (machine_.Reg[2] == UPPER + 1) fail(StackExceptionCause::STACK_UNDERFLOW, machine_.Reg[2], "stack underflow");
    }
    void checkAddress(int address) const {
        if (address < LOWER || address > UPPER) fail(StackExceptionCause::STACK_ADDRESS_VIOLATION, address, "stack address outside reserved region");
    }
    void checkPointer() const {
        if (machine_.Reg[2] < LOWER || machine_.Reg[2] > UPPER + 1)
            fail(StackExceptionCause::STACK_POINTER_CORRUPTION, machine_.Reg[2], "invalid stack pointer");
    }
    void push(int value) { checkPush(); machine_.memory[--machine_.Reg[2]] = value; }
    int pop() { checkPop(); int value = machine_.memory[machine_.Reg[2]++]; return value; }
private:
    MachineState& machine_;
    [[noreturn]] static void fail(StackExceptionCause c, int a, const std::string& m) { throw StackRuntimeException(c, a, m); }
};
