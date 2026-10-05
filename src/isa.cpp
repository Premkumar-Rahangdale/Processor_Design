// #include "include/isa.hpp"

// constexpr uint32_t opcodeBits(Opcode op) {
//     return static_cast<uint32_t>(op) << OPCODE_SHIFT;
// }

// constexpr bool isValidOpcode(uint32_t raw5) {
//     return raw5 >= static_cast<uint32_t>(Opcode::Add) && raw5 <= static_cast<uint32_t>(Opcode::Ret);
// }

// constexpr Opcode decodeOpcode(uint32_t word) {
//     uint32_t raw = (word >> OPCODE_SHIFT) & OPCODE_MASK;
//     if (!isValidOpcode(raw)) return static_cast<Opcode>(0b11111);
//     return static_cast<Opcode>(raw);
// }

// constexpr std::string_view mnemonic(Opcode opcode){
//     switch (opcode) {
//         // Arithmetic & Logical
//         case Opcode::Add: return "add";   
//         case Opcode::Sub: return "sub";
//         case Opcode::Mul: return "mul";   
//         case Opcode::Div: return "div";
//         case Opcode::Mod: return "mod";   
//         case Opcode::Cmp: return "cmp";
//         case Opcode::And: return "and";   
//         case Opcode::Or:  return "or";
//         case Opcode::Not: return "not";   
//         case Opcode::Xor: return "xor";
//         // memory transfer and shift
//         case Opcode::Mov: return "mov";   
//         case Opcode::Movu: return "movu";
//         case Opcode::Movh: return "movh"; 
//         case Opcode::Lsl: return "lsl";
//         case Opcode::Lsr: return "lsr";   
//         case Opcode::Asr: return "asr";
//         case Opcode::Nop: return "nop";   
//         case Opcode::Ld:  return "ld";
//         case Opcode::St: return "st";  
//         // branch   
//         case Opcode::Beq: return "beq";
//         case Opcode::Bgt: return "bgt";   
//         case Opcode::Bsm: return "bsm";
//         case Opcode::B: return "b";       
//         case Opcode::Call: return "call";
//         case Opcode::Ret: return "ret";
//     }
//     return "?";
// }