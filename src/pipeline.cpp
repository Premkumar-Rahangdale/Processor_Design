#include "../include/pipeline.hpp"
#include <iomanip>
#include <iostream>

namespace {
constexpr int kStackLowerBound = 3072;
constexpr int kStackUpperBound = 4095;
constexpr int kStackEmptyPointer = kStackUpperBound + 1;
}

Pipeline::Pipeline(std::vector<Stage> layout, std::vector<std::string> names)
    : layout_(std::move(layout)), names_(std::move(names))
{
    slot_.assign(layout_.size(), Latch{});
    for (int s = 0; s < static_cast<int>(layout_.size()); s++) {
        if (layout_[s] == Stage::ID) idIdx_ = s;
        if (layout_[s] == Stage::MEM || layout_[s] == Stage::EXMEM) memIdx_ = s;
    }
    // the operands are read in RR if there is one, otherwise in ID
    readIdx_ = idIdx_;
    for (int s = 0; s < static_cast<int>(layout_.size()); s++)
        if (layout_[s] == Stage::RR) readIdx_ = s;
}

int Pipeline::field(const std::string& s, int start, int len) {
    int v = 0;
    for (int i = 0; i < len; i++) v = (v << 1) | (s[start + i] - '0');
    return v;
}

int Pipeline::signExtend(int v, int bits) {
    int m = 1 << (bits - 1);
    return (v ^ m) - m;
}

// ---------------------------------------------------------------------------
//  Running
// ---------------------------------------------------------------------------
void Pipeline::begin(MachineState& m) {
    m.reset();                                   // does not touch instrMemory / labels
    m.Reg[2] = kStackEmptyPointer;               // full-descending stack starts empty
    for (Latch& l : slot_) l = Latch{};
    halted_ = false;
    faultCause_ = 0;
    executed_ = cycles_ = dataStalls_ = flushes_ = squashed_ = ctlBubbles_ = 0;
    branches_ = mispredicts_ = 0;
    btb_.clear();
    lastRetiredPc_ = -1;
    sFz_ = m.zero; sFn_ = m.neg; sFo_ = m.over; sFc_ = m.carry;
    fetchDelay_ = decodePenalty();               // the control path has to fill before the first instruction
    prefetch(m);                                 // the IF of clock 1
}

// IF for the next clock: a bubble while the control path refills, otherwise the next instruction
void Pipeline::prefetch(MachineState& m) {
    if (fetchDelay_ > 0) { --fetchDelay_; ++ctlBubbles_; }
    else fetchInto(m, slot_[0]);
}

void Pipeline::run(MachineState& m) {
    begin(m);
    while (tick(m)) {}

    if (executed_ >= kMaxInstructions || cycles_ >= kMaxCycles)
        std::cerr << "[pipeline] stopped: instruction/cycle limit reached\n";

    if (trace) {
        std::cout << "---- " << name() << ": " << executed_ << " instructions, " << cycles_ << " cycles, CPI "
                  << std::fixed << std::setprecision(2) << cpi() << ", data stalls " << dataStalls_
                  << ", flushes " << flushes_ << " (" << squashed_ << " squashed), control bubbles "
                  << ctlBubbles_ << ", branches " << branches_ << " (" << mispredicts_ << " mispredicted) ----\n";
        for (int i = 0; i < static_cast<int>(m.Reg.size()); i++)
            if (m.Reg[i] != 0) std::cout << "r" << i << " = " << m.Reg[i] << "\n";
        for (int i = 0; i < static_cast<int>(m.memory.size()); i++)
            if (m.memory[i] != 0) std::cout << "mem[" << i << "] = " << m.memory[i] << "\n";
    }
}

bool Pipeline::fetchHasWork(const MachineState& m) const {
    for (int p = m.pc; p >= 0 && p < static_cast<int>(m.instrMemory.size()); p++) {
        const std::string& w = m.instrMemory[p];
        if (w.empty() || w[0] != '.') return true;
    }
    return false;
}

bool Pipeline::finished(const MachineState& m) const {
    if (halted_ || executed_ >= kMaxInstructions || cycles_ >= kMaxCycles) return true;
    for (const Latch& l : slot_) if (l.valid) return false;
    return !fetchHasWork(m);
}

int Pipeline::nextPc(const MachineState& m) const {
    if (finished(m)) return -1;
    for (int s = stageCount() - 1; s >= 0; s--)
        if (slot_[s].valid) return slot_[s].pc;
    for (int p = m.pc; p >= 0 && p < static_cast<int>(m.instrMemory.size()); p++) {
        const std::string& w = m.instrMemory[p];
        if (w.empty() || w[0] != '.') return p;
    }
    return -1;
}

bool Pipeline::step(MachineState& m) {
    const long before = executed_;
    while (!finished(m) && executed_ == before) tick(m);
    return executed_ > before;
}

int Pipeline::stepN(MachineState& m, int n, std::vector<int>* executedPcs) {
    int count = 0;
    while (count < n && step(m)) {
        count++;
        if (executedPcs) executedPcs->push_back(lastRetiredPc_);
    }
    return count;
}

// ---------------------------------------------------------------------------
//  One clock
// ---------------------------------------------------------------------------
bool Pipeline::tick(MachineState& m) {
    if (finished(m)) return false;
    ++cycles_;

    const int n = stageCount();
    const std::vector<Latch> snap = slot_;       // state at the start of the clock (what the latches hold)
    bool stall = false, redirected = false;
    int target = 0;

    // ---- WB: commit (or take the exception). It runs first, so a register written
    //      here is already visible to a read in this same clock.
    {
        Latch& w = slot_[n - 1];
        if (w.valid) {
            if (w.fault != pcause::NONE) {
                takeException(m, w);
                if (trace) traceLine(snap, false, false);
                return true;
            }
            retire(m, w);
            w = Latch{};
        }
    }

    // ---- ID: the control unit produces the control signals
    if (slot_[idIdx_].valid) decode(slot_[idIdx_]);

    // ---- RAW hazard on the instruction that is about to read its operands
    if (slot_[readIdx_].valid && hasHazard(slot_[readIdx_], snap)) stall = true;

    // ---- stage work, oldest instruction first
    const int U = readIdx_ + 1;                  // first execution stage; control transfers resolve here
    for (int s = n - 2; s >= 1 && !redirected; --s) {
        Latch& L = slot_[s];
        if (!L.valid) continue;
        switch (layout_[s]) {
            case Stage::MEM:
                memStage(m, L);
                break;
            case Stage::EX:
                exStage(m, L, snap, s);
                break;
            case Stage::EXMEM:
                exStage(m, L, snap, s);
                memStage(m, L);
                break;
            case Stage::RR:
            case Stage::ID:
                if (s == readIdx_ && !stall) readOperands(m, L);
                break;
            default:
                break;
        }
        if ((layout_[s] == Stage::EX || layout_[s] == Stage::EXMEM) && L.fault == pcause::NONE)
            redirected = resolveBranch(L, target);
    }

    if (redirected) {                            // misprediction: squash the younger instructions
        for (int s = 0; s < U; s++) {
            if (slot_[s].valid) ++squashed_;
            slot_[s] = Latch{};
        }
        m.pc = target;
        ++flushes_;
        fetchDelay_ = decodePenalty();
        stall = false;
    }
    if (stall) ++dataStalls_;

    // ---- advance the instructions (a stall holds IF..read stage and sends a bubble on)
    for (int s = n - 1; s > readIdx_ + 1; --s) slot_[s] = slot_[s - 1];
    slot_[readIdx_ + 1] = stall ? Latch{} : slot_[readIdx_];
    if (!stall) {
        for (int s = readIdx_; s >= 1; --s) slot_[s] = slot_[s - 1];
        slot_[0] = Latch{};
    }

    // ---- IF of the next clock (after a redirect: the target; held while stalled)
    if (!stall) prefetch(m);

    if (trace) traceLine(snap, stall, redirected);
    return true;
}

// ---------------------------------------------------------------------------
//  IF: fetch instrMemory[pc] and advance the pc; label lines cost nothing
// ---------------------------------------------------------------------------
bool Pipeline::fetchInto(MachineState& m, Latch& out) {
    const int size = static_cast<int>(m.instrMemory.size());
    while (m.pc >= 0 && m.pc < size && !m.instrMemory[m.pc].empty() && m.instrMemory[m.pc][0] == '.') m.pc++;
    if (m.pc < 0 || m.pc >= size) return false;             // nothing to fetch (end of program / wrong path)

    const std::string& word = m.instrMemory[m.pc];
    Latch L;
    L.valid = true;
    L.pc = m.pc;
    L.raw = word;
    if (word.size() != 32) {
        L.fault = pcause::ILLEGAL_OPCODE;
        L.faultMsg = "bad instruction word at index " + std::to_string(m.pc);
    }
    m.pc++;                                                  // EX overwrites this on a misprediction
    if (predictor == Predictor::TWO_BIT && L.fault == pcause::NONE) {
        auto it = btb_.find(L.pc);                           // hit = a control transfer seen before
        if (it != btb_.end() && it->second.counter >= 2) {
            L.predTaken = true;                              // predicted taken: fetch continues at the target
            L.predTarget = it->second.target;
            m.pc = L.predTarget;
        }
    }
    out = L;
    return true;
}

// ---------------------------------------------------------------------------
//  ID: control signals + instruction fields
// ---------------------------------------------------------------------------
void Pipeline::decode(Latch& L) {
    if (L.decoded || L.fault != pcause::NONE) return;
    L.decoded = true;

    generatecontrolsignals(L.raw.substr(0, 6));              // opcode + I bit (hardwired or from the control memory)
    L.sig = signals;

    if (!L.sig.isKnown()) {
        L.sig = controlsignals{};
        L.fault = pcause::ILLEGAL_OPCODE;
        L.faultMsg = "illegal opcode at index " + std::to_string(L.pc);
        return;
    }

    L.rd     = field(L.raw, 6, 5);
    L.rs1    = field(L.raw, 11, 5);
    L.rs2    = field(L.raw, 16, 5);
    L.imm    = signExtend(field(L.raw, 16, 16), 16);
    L.offset = signExtend(field(L.raw, 5, 27), 27);          // branches only
    L.dest   = L.sig.writesReg() ? (L.sig.isCall ? kRa : L.rd) : 0;
}

// Registers an instruction reads (r0 is never a hazard).
void Pipeline::sources(const Latch& L, int out[4], int& n) {
    n = 0;
    const controlsignals& s = L.sig;
    auto add = [&](int r) { if (r != 0) out[n++] = r; };
    if (s.readsRs1()) add(L.rs1);
    if (s.usesOperandB() && !s.isImm) add(L.rs2);
    if (s.isSt) add(L.rd);                                   // st: rd is the source of the data
    if (s.isRet) add(kRa);                                   // ret: return address
}

// ---------------------------------------------------------------------------
//  RAW hazards. `snap` holds the older instructions (higher stage index).
//    stall only : the value must already be in the register file
//    forwarding : only a load whose data is not available yet is a hazard
// ---------------------------------------------------------------------------
bool Pipeline::hasHazard(const Latch& C, const std::vector<Latch>& snap) const {
    if (!C.valid || C.fault != pcause::NONE) return false;
    int src[4], ns;
    sources(C, src, ns);
    if (ns == 0) return false;

    const int n = stageCount();
    for (int t = readIdx_ + 1; t < n; t++) {
        const Latch& w = snap[t];
        if (!w.valid || w.fault != pcause::NONE || w.dest == 0) continue;
        bool uses = false;
        for (int i = 0; i < ns; i++) if (src[i] == w.dest) uses = true;
        if (!uses) continue;

        if (forwarding) {
            if (w.sig.isLd && t < memIdx_) return true;      // load data not produced yet
        } else {
            if (t < n - 1 || !writeThrough) return true;     // not yet in the register file
        }
    }
    return false;
}

// RR (or ID in the 4-stage processor): read the register file
void Pipeline::readOperands(MachineState& m, Latch& L) {
    if (L.fault != pcause::NONE) return;
    const controlsignals& s = L.sig;
    auto reg = [&m](int i) { return i == 0 ? 0 : m.Reg[i]; };   // R0 always reads 0

    if (s.readsRs1())      L.opA = reg(L.rs1);
    if (s.isRet)           L.opA = reg(kRa);
    if (s.usesOperandB())  L.opB = s.isImm ? L.imm : reg(L.rs2);
    if (s.isSt)            L.storeVal = reg(L.rd);
}

// Operand forwarding: take the value from the youngest older instruction that writes the register.
void Pipeline::forwardOperands(Latch& L, const std::vector<Latch>& snap, int stageIdx) const {
    const controlsignals& s = L.sig;
    const int n = stageCount();
    auto fwd = [&](int r, int& value) {
        if (r == 0) return;
        for (int t = stageIdx + 1; t < n; t++) {
            const Latch& w = snap[t];
            if (w.valid && w.fault == pcause::NONE && w.dest == r) { value = w.writeData; return; }
        }
    };
    if (s.readsRs1())                 fwd(L.rs1, L.opA);
    if (s.isRet)                      fwd(kRa, L.opA);
    if (s.usesOperandB() && !s.isImm) fwd(L.rs2, L.opB);
    if (s.isSt)                       fwd(L.rd, L.storeVal);
}

// ---------------------------------------------------------------------------
//  EX: ALU work, effective address, branch decision
// ---------------------------------------------------------------------------
void Pipeline::exStage(MachineState& m, Latch& L, const std::vector<Latch>& snap, int stageIdx) {
    (void)m;
    if (L.fault != pcause::NONE) return;
    if (forwarding) forwardOperands(L, snap, stageIdx);
    const controlsignals& s = L.sig;

    if (s.isCall) {
        L.aluResult = L.pc + 1;                              // link value for ra
    } else if (s.isLd || s.isSt || s.writesReg() || s.isCmp) {
        ALUResult r = alu_unit.execute(s, L.opA, L.opB);     // ld/st: address = base + offset
        if (r.divByZero) {
            L.fault = pcause::DIV_BY_ZERO;
            L.faultMsg = "division by zero at index " + std::to_string(L.pc);
            return;
        }
        L.aluResult = static_cast<int>(r.value);
        if (L.dest == 2) {
            const int newSp = L.aluResult;
            if (newSp < kStackLowerBound) {
                L.fault = pcause::STACK_OVERFLOW;
                L.faultMsg = "stack overflow: stack pointer crossed the lower boundary";
                return;
            }
            if (newSp > kStackEmptyPointer) {
                L.fault = pcause::STACK_POINTER_CORRUPTION;
                L.faultMsg = "stack pointer corruption: r2 is outside the permitted stack range";
                return;
            }
        }
        if (r.updateFlags) {                                 // only cmp sets the flags
            sFz_ = r.zero; sFn_ = r.neg; sFo_ = r.over; sFc_ = r.carry;
            L.setsFlags = true;
            L.fz = r.zero; L.fn = r.neg; L.fo = r.over; L.fc = r.carry;
        }
    }

    bool taken = false;
    int target = 0;
    if (s.isB || s.isCall)  { taken = true;               target = L.pc + L.offset; }
    else if (s.isBeq)       { taken = sFz_;               target = L.pc + L.offset; }
    else if (s.isBgt)       { taken = !sFz_ && (sFn_ == sFo_); target = L.pc + L.offset; }
    else if (s.isBsm)       { taken = (sFn_ != sFo_);     target = L.pc + L.offset; }
    else if (s.isRet)       { taken = true;               target = L.opA; }

    L.branchTaken = taken;
    L.branchTarget = target;

    if (!s.isLd) { L.writeData = L.aluResult; L.resultReady = true; }
}

// MEM: data memory access (word-addressed)
void Pipeline::memStage(MachineState& m, Latch& L) {
    if (L.fault != pcause::NONE) return;
    const controlsignals& s = L.sig;
    if (s.isLd || s.isSt) {
        int addr = L.aluResult;
        if (L.rs1 == 2) {
            if (s.isLd && addr == kStackEmptyPointer) {
                L.fault = pcause::STACK_UNDERFLOW;
                L.faultMsg = "stack underflow: pop attempted on an empty stack";
                return;
            }
            if (addr < kStackLowerBound || addr > kStackUpperBound) {
                L.fault = s.isSt ? pcause::STACK_OVERFLOW : pcause::STACK_ADDRESS_VIOLATION;
                L.faultMsg = s.isSt
                    ? "stack overflow: push crossed the lower stack boundary"
                    : "stack access outside configured stack boundaries";
                return;
            }
        }
        if (addr < 0 || addr >= static_cast<int>(m.memory.size())) {
            L.fault = pcause::MEM_ADDRESS;
            L.faultMsg = "memory address out of range: " + std::to_string(addr);
            return;                                          // the access does not happen
        }
        if (s.isLd) L.memData = m.memory[addr];
        if (s.isSt) m.memory[addr] = L.storeVal;
    }
    if (s.isLd) { L.writeData = L.memData; L.resultReady = true; }
}

// WB: write the result back, commit the flags
void Pipeline::retire(MachineState& m, Latch& L) {
    if (L.dest != 0) m.Reg[L.dest] = L.writeData;            // writes to R0 are ignored (dest is never 0 here)
    if (L.setsFlags) { m.zero = L.fz; m.neg = L.fn; m.over = L.fo; m.carry = L.fc; }
    ++executed_;
    lastRetiredPc_ = L.pc;
}

// ---------------------------------------------------------------------------
//  Branch resolution (EX): compare the prediction with what really happened,
//  train the predictor. Returns true on a misprediction; `target` is where the
//  fetch has to restart (the real target, or the next instruction if the
//  branch was predicted taken but is not).
// ---------------------------------------------------------------------------
bool Pipeline::resolveBranch(Latch& L, int& target) {
    const controlsignals& s = L.sig;
    if (!(s.usesOffset() || s.isRet)) return false;          // not a control transfer

    ++branches_;
    const bool taken = L.branchTaken;
    const bool wrong = taken ? (!L.predTaken || L.predTarget != L.branchTarget) : L.predTaken;

    if (predictor == Predictor::TWO_BIT) {                   // train: a taken branch gets an entry
        auto it = btb_.find(L.pc);
        if (taken) {
            if (it == btb_.end()) it = btb_.emplace(L.pc, BtbEntry{}).first;
            if (it->second.counter < 3) it->second.counter++;
            it->second.target = L.branchTarget;
        } else if (it != btb_.end() && it->second.counter > 0) {
            it->second.counter--;
        }
    }

    if (!wrong) return false;
    ++mispredicts_;
    target = taken ? L.branchTarget : L.pc + 1;
    return true;
}

// Precise exception: everything older has completed, everything younger is thrown away.
void Pipeline::takeException(MachineState& m, const Latch& faulting) {
    const Latch L = faulting;                                // a copy: the slot it lives in is cleared below
    std::cerr << "[pipeline] " << L.faultMsg << "\n";
    m.Reg[4] = L.fault;                                      // R4: cause
    m.Reg[5] = L.pc;                                         // R5: index of the faulting instruction
    for (Latch& l : slot_) { if (l.valid) ++squashed_; l = Latch{}; }
    sFz_ = m.zero; sFn_ = m.neg; sFo_ = m.over; sFc_ = m.carry;
    halted_ = true;
    faultCause_ = L.fault;
}

// ---------------------------------------------------------------------------
//  Trace: one line per clock, the instruction index in every stage
// ---------------------------------------------------------------------------
void Pipeline::traceLine(const std::vector<Latch>& snap, bool stall, bool redirected) const {
    std::cout << "cycle " << std::setw(4) << cycles_ << " |";
    for (int s = 0; s < stageCount(); s++) {
        std::cout << " " << names_[s] << ":";
        if (snap[s].valid) std::cout << snap[s].pc;
        else std::cout << "--";
    }
    if (stall) std::cout << "  STALL";
    if (redirected) std::cout << "  FLUSH";
    if (halted_) std::cout << "  EXCEPTION";
    std::cout << "\n";
}
