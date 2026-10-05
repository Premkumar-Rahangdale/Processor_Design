#pragma once
#include <cstdint>
#include <array>
#include <cstddef>
#include <string_view>

constexpr unsigned OPCODE_BITS  = 5;
constexpr unsigned OPCODE_SHIFT = 32 - OPCODE_BITS;
constexpr uint32_t OPCODE_MASK  = (1u << OPCODE_BITS) - 1;

enum class Opcode : uint8_t{
    // Arithmetic
    Add = 0b00001, Sub = 0b00010, Mul = 0b00011, Div = 0b00100, Mod = 0b00101,
    Cmp = 0b00110, And = 0b00111, Or  = 0b01000, Not = 0b01001, Xor = 0b01010,
    // memory transfer and shift
    Mov = 0b01011, Movu = 0b01100, Movh = 0b01101,
    Lsl = 0b01110, Lsr = 0b01111, Asr = 0b10000,
    Nop = 0b10001, Ld = 0b10010, St = 0b10011,
    // branch
    Beq = 0b10100, Bgt = 0b10101, Bsm = 0b10110, B = 0b10111,
    Call = 0b11000, Ret = 0b11001
};

constexpr uint32_t opcodeBits(Opcode op) {}

constexpr bool isValidOpcode(uint32_t raw5) {}

constexpr Opcode decodeOpcode(uint32_t word) {}

constexpr std::string_view mnemonic(Opcode opcode) {}