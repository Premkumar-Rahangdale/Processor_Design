#pragma once

#include <string>
#include "MachineState.hpp"

#include <stdexcept>

enum class StackExceptionCause {
    STACK_OVERFLOW,
    STACK_UNDERFLOW,
    STACK_POINTER_CORRUPTION,
    STACK_ADDRESS_VIOLATION
};

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
    explicit StackExceptionFramework(MachineState& state,
                                     int lowerBound = 0,
                                     int upperBound = -1)
        : machine_(state), lower_(lowerBound),
          upper_(upperBound < 0 ? static_cast<int>(state.memory.size()) - 1 : upperBound) {
        if (machine_.memory.empty() || lower_ < 0 ||
            upper_ >= static_cast<int>(machine_.memory.size()) || lower_ > upper_)
            throw std::invalid_argument("invalid stack memory boundaries");
    }

    int lowerBound() const noexcept { return lower_; }
    int upperBound() const noexcept { return upper_; }

    // Full-descending convention: an empty stack has SP = upperBound + 1.
    // After a push, SP points to the newly occupied word.
    void initialize() {
        machine_.Reg[2] = upper_ + 1;
        initialized_ = true;
    }

    bool initialized() const noexcept { return initialized_; }

    void checkPush() const {
        checkStackPointer(true);
        const int nextAddress = machine_.Reg[2] - 1;
        if (nextAddress < lower_)
            raise(StackExceptionCause::STACK_OVERFLOW, nextAddress,
                  "stack overflow: push would cross the lower stack boundary");
        checkStackAddress(nextAddress);
    }

    void checkPop() const {
        checkStackPointer(true);
        const int sp = machine_.Reg[2];
        if (sp == upper_ + 1)
            raise(StackExceptionCause::STACK_UNDERFLOW, sp,
                  "stack underflow: pop attempted on an empty stack");
        checkStackAddress(sp);
    }

    void checkStackAddress(int address) const {
        if (address < lower_ || address > upper_)
            raise(StackExceptionCause::STACK_ADDRESS_VIOLATION, address,
                  "stack memory access outside configured stack boundaries");
    }

    void checkStackPointer(bool allowEmpty = true) const {
        const int sp = machine_.Reg[2];
        const int maximum = allowEmpty ? upper_ + 1 : upper_;
        if (sp < lower_ || sp > maximum)
            raise(StackExceptionCause::STACK_POINTER_CORRUPTION, sp,
                  "stack pointer is outside the permitted stack range");
    }

    // These helpers provide checked stack operations for host-side simulation/tests.
    void push(int value) {
        checkPush();
        const int address = machine_.Reg[2] - 1;
        machine_.Reg[2] = address;
        machine_.memory[address] = value;
    }

    int pop() {
        checkPop();
        const int address = machine_.Reg[2];
        const int value = machine_.memory[address];
        machine_.Reg[2] = address + 1;
        return value;
    }

    [[noreturn]] static void raise(StackExceptionCause cause, int address,
                                   const std::string& message) {
        throw StackRuntimeException(cause, address, message);
    }

private:
    MachineState& machine_;
    int lower_;
    int upper_;
    bool initialized_ = false;
};
