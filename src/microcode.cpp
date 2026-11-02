#include "../include/microcode.hpp"
#include <stdexcept>
#include <unordered_map>

namespace {

using Rows = std::vector<MicroInstr>;

// ---------------------------------------------------------------------------
//  3-address ALU instructions:  op rd, rs1, rs2/imm
//  (book section 8.6.4; one private copy of the routine per instruction
//  because the ALU operation is part of the microinstruction)
// ---------------------------------------------------------------------------
void mcAlu3(Rows& p, const std::string& name, uint16_t aluop) {
    using namespace mi;
    const std::string L = "." + name;
    p.push_back(mmov(MReg::REG_SRC, MReg::RS1, margs::READ).at(L));      // A <- Reg[rs1]
    p.push_back(mmov(MReg::A, MReg::REG_VAL));
    p.push_back(mbeq(MReg::I, 1, L + "_imm"));                            // second operand an immediate?
    p.push_back(mmov(MReg::REG_SRC, MReg::RS2, margs::READ));             // B <- Reg[rs2], start the ALU
    p.push_back(mmov(MReg::B, MReg::REG_VAL, aluop));
    p.push_back(mb(L + "_rw"));
    p.push_back(mmov(MReg::B, MReg::IMMX, aluop).at(L + "_imm"));         // B <- immx, start the ALU
    p.push_back(mmov(MReg::REG_SRC, MReg::RD).at(L + "_rw"));             // Reg[rd] <- aluResult
    p.push_back(mmov(MReg::REG_DATA, MReg::ALU_RESULT, margs::WRITE));
    p.push_back(mb(".begin"));
}

// ---------------------------------------------------------------------------
//  2-address instructions that go through the ALU: mov, movu, movh, not
//  (operand in rs2/imm, rs1 unused; section 8.6.5, ALU version)
// ---------------------------------------------------------------------------
void mcAlu2(Rows& p, const std::string& name, uint16_t aluop) {
    using namespace mi;
    const std::string L = "." + name;
    p.push_back(mbeq(MReg::I, 1, L + "_imm").at(L));
    p.push_back(mmov(MReg::REG_SRC, MReg::RS2, margs::READ));
    p.push_back(mmov(MReg::B, MReg::REG_VAL, aluop));
    p.push_back(mb(L + "_rw"));
    p.push_back(mmov(MReg::B, MReg::IMMX, aluop).at(L + "_imm"));
    p.push_back(mmov(MReg::REG_SRC, MReg::RD).at(L + "_rw"));
    p.push_back(mmov(MReg::REG_DATA, MReg::ALU_RESULT, margs::WRITE));
    p.push_back(mb(".begin"));
}

// cmp rs1, rs2/imm : no destination register, only the flags change
void mcCmp(Rows& p) {
    using namespace mi;
    p.push_back(mmov(MReg::REG_SRC, MReg::RS1, margs::READ).at(".cmp"));
    p.push_back(mmov(MReg::A, MReg::REG_VAL));
    p.push_back(mbeq(MReg::I, 1, ".cmp_imm"));
    p.push_back(mmov(MReg::REG_SRC, MReg::RS2, margs::READ));
    p.push_back(mmov(MReg::B, MReg::REG_VAL, margs::ALU_CMP));
    p.push_back(mb(".begin"));
    p.push_back(mmov(MReg::B, MReg::IMMX, margs::ALU_CMP).at(".cmp_imm"));
    p.push_back(mb(".begin"));
}

// conditional branch: take it when the chosen 1-bit flag is set
void mcBranchIf(Rows& p, const std::string& name, MReg flag) {
    using namespace mi;
    const std::string L = "." + name;
    p.push_back(mbeq(flag, 1, L + "_taken").at(L));
    p.push_back(mb(".begin"));
    p.push_back(mmov(MReg::PC, MReg::BRANCH_TARGET).at(L + "_taken"));
    p.push_back(mb(".begin"));
}

}  // namespace

MicroProgram buildRisc201Microprogram() {
    using namespace mi;
    MicroProgram prog;
    Rows& p = prog.words;

    // ---- preamble: fetch, decode, pc <- pc + 1, dispatch on the opcode ------
    p.push_back(mloadIR().at(".begin"));
    p.push_back(mdecode());
    p.push_back(madd(MReg::PC, 1));
    p.push_back(mswitch());

    // ---- arithmetic, logic, shifts ------------------------------------------
    mcAlu3(p, "add", margs::ALU_ADD);
    mcAlu3(p, "sub", margs::ALU_SUB);
    mcAlu3(p, "mul", margs::ALU_MUL);
    mcAlu3(p, "div", margs::ALU_DIV);
    mcAlu3(p, "mod", margs::ALU_MOD);
    mcCmp(p);
    mcAlu3(p, "and", margs::ALU_AND);
    mcAlu3(p, "or",  margs::ALU_OR);
    mcAlu2(p, "not", margs::ALU_NOT);
    mcAlu3(p, "xor", margs::ALU_XOR);
    mcAlu2(p, "mov",  margs::ALU_MOV);
    mcAlu2(p, "movu", margs::ALU_MOVU);
    mcAlu2(p, "movh", margs::ALU_MOVH);
    mcAlu3(p, "lsl", margs::ALU_LSL);
    mcAlu3(p, "lsr", margs::ALU_LSR);
    mcAlu3(p, "asr", margs::ALU_ASR);

    // ---- nop ------------------------------------------------------------------
    p.push_back(mb(".begin").at(".nop"));

    // ---- ld rd, imm[rs1] : address = Reg[rs1] + imm (the I bit is always 1) ----
    p.push_back(mmov(MReg::REG_SRC, MReg::RS1, margs::READ).at(".ld"));
    p.push_back(mmov(MReg::A, MReg::REG_VAL));
    p.push_back(mmov(MReg::B, MReg::IMMX, margs::ALU_ADD));               // effective address
    p.push_back(mmov(MReg::MAR, MReg::ALU_RESULT, margs::LOAD));
    p.push_back(mmov(MReg::REG_DATA, MReg::LD_RESULT));
    p.push_back(mmov(MReg::REG_SRC, MReg::RD, margs::WRITE));
    p.push_back(mb(".begin"));

    // ---- st rd, imm[rs1] : rd is the source of the data -------------------------
    p.push_back(mmov(MReg::REG_SRC, MReg::RS1, margs::READ).at(".st"));
    p.push_back(mmov(MReg::A, MReg::REG_VAL));
    p.push_back(mmov(MReg::B, MReg::IMMX, margs::ALU_ADD));               // effective address
    p.push_back(mmov(MReg::MAR, MReg::ALU_RESULT));
    p.push_back(mmov(MReg::REG_SRC, MReg::RD, margs::READ));
    p.push_back(mmov(MReg::MDR, MReg::REG_VAL, margs::STORE));
    p.push_back(mb(".begin"));

    // ---- branches (branchTarget was latched by mdecode) ---------------------------
    mcBranchIf(p, "beq", MReg::FLAG_Z);
    mcBranchIf(p, "bgt", MReg::FLAG_GT);
    mcBranchIf(p, "bsm", MReg::FLAG_LT);

    p.push_back(mmov(MReg::PC, MReg::BRANCH_TARGET).at(".b"));
    p.push_back(mb(".begin"));

    // ---- call: ra <- pc (already the next instruction), pc <- branchTarget -------
    p.push_back(mmov(MReg::REG_DATA, MReg::PC).at(".call"));
    p.push_back(mmovi(MReg::REG_SRC, 1, margs::WRITE));                    // r1 = ra
    p.push_back(mmov(MReg::PC, MReg::BRANCH_TARGET));
    p.push_back(mb(".begin"));

    // ---- ret: pc <- ra ---------------------------------------------------------------
    p.push_back(mmovi(MReg::REG_SRC, 1, margs::READ).at(".ret"));
    p.push_back(mmov(MReg::PC, MReg::REG_VAL));
    p.push_back(mb(".begin"));

    // ---- exception routine ------------------------------------------------------------
    //   R4 <- cause, R5 <- index of the faulting instruction (pc was already
    //   incremented by the preamble), then stop.
    p.push_back(mmov(MReg::REG_DATA, MReg::EXC_CAUSE).at(".exception"));
    p.push_back(mmovi(MReg::REG_SRC, 4, margs::WRITE));
    p.push_back(mmov(MReg::REG_DATA, MReg::PC));
    p.push_back(madd(MReg::REG_DATA, -1));
    p.push_back(mmovi(MReg::REG_SRC, 5, margs::WRITE));
    p.push_back(mmov(MReg::REG_SRC, MReg::REG_SRC, margs::HALT));           // <halt> (the move is redundant)

    // ---- opcode -> routine (the switch unit's table) ---------------------------------
    static const char* const names[26] = {
        nullptr, "add", "sub", "mul", "div", "mod", "cmp", "and", "or", "not", "xor", "mov", "movu",
        "movh", "lsl", "lsr", "asr", "nop", "ld", "st", "beq", "bgt", "bsm", "b", "call", "ret"
    };
    for (int op = 1; op <= 25; op++) prog.opcodeLabel[op] = std::string(".") + names[op];

    return prog;
}

AssembledMicroProgram assembleMicroProgram(const MicroProgram& prog) {
    AssembledMicroProgram out;
    out.words = prog.words;
    if (out.words.size() > static_cast<size_t>(kControlMemWords))
        throw std::runtime_error("microprogram does not fit in the control memory");

    // pass 1: labels
    std::unordered_map<std::string, int> addr;
    for (size_t i = 0; i < out.words.size(); i++) {
        const std::string& l = out.words[i].label;
        if (l.empty()) continue;
        if (!addr.emplace(l, static_cast<int>(i)).second)
            throw std::runtime_error("duplicate micro-label " + l);
    }
    auto find = [&addr](const std::string& l) {
        auto it = addr.find(l);
        if (it == addr.end()) throw std::runtime_error("undefined micro-label " + l);
        return it->second;
    };

    // pass 2: targets
    out.targetAddr.assign(out.words.size(), 0);
    for (size_t i = 0; i < out.words.size(); i++) {
        const MicroOp op = out.words[i].op;
        if (op == MicroOp::MB || op == MicroOp::MBEQ)
            out.targetAddr[i] = static_cast<uint16_t>(find(out.words[i].target));
    }
    out.beginAddr = static_cast<uint16_t>(find(prog.beginLabel));
    out.exceptionAddr = static_cast<uint16_t>(find(prog.exceptionLabel));
    for (int op = 0; op < 32; op++)
        out.switchAddr[op] = prog.opcodeLabel[op].empty() ? static_cast<int16_t>(-1)
                                                          : static_cast<int16_t>(find(prog.opcodeLabel[op]));
    return out;
}
