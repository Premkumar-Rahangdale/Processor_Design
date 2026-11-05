#include "../include/microcontrolunit.hpp"
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
//  Common part
// ---------------------------------------------------------------------------
void MicroControlUnit::load(const MicroProgram& prog) {
    AssembledMicroProgram img = assembleMicroProgram(prog);
    resizeMemory(kControlMemWords);
    for (size_t i = 0; i < img.words.size(); i++) {
        // validate the register connections before storing
        signalsFor(img.words[i].op, img.words[i].dst, img.words[i].src, img.words[i].imm, img.targetAddr[i], img.words[i].args);
        store(static_cast<int>(i), img.words[i], img.targetAddr[i]);
    }
    image_ = std::move(img);
    loaded_ = true;
    buildDecodeTable();
}

// ---------------------------------------------------------------------------
//  Decoder interface: control memory -> controlsignals
// ---------------------------------------------------------------------------
controlsignals MicroControlUnit::controlSignalsFor(const std::string& opcodeAndI) const {
    controlsignals s{};
    if (!loaded_ || opcodeAndI.size() < 6) return s;
    unsigned op = 0;
    for (int i = 0; i < 5; i++) op = (op << 1) | static_cast<unsigned>(opcodeAndI[i] - '0');   // MSB first
    s = decode_[op & 31u];
    s.isImm = (opcodeAndI[5] == '1');
    return s;
}

void MicroControlUnit::buildDecodeTable() {
    for (int op = 0; op < 32; op++) {
        const int a = image_.switchAddr[op];
        decode_[op] = (a >= 0) ? analyseRoutine(a) : controlsignals{};
    }
}

// Follows every path of the routine (both sides of each mbeq) up to its
// "mb .begin" and records what the micro-operations on the way do.
controlsignals MicroControlUnit::analyseRoutine(int startAddr) const {
    enum Guard { G_NONE = 0, G_Z, G_GT, G_LT };
    auto bit = [](uint32_t v, MReg r) { return ((v >> static_cast<int>(r)) & 1u) != 0; };

    controlsignals c{};
    bool pcFromTargetUnguarded = false, pcFromRegVal = false, writesRa = false;
    bool beq = false, bgt = false, bsm = false;

    std::vector<char> seen(image_.words.size() * 4, 0);
    std::vector<std::pair<int, int>> todo{{startAddr, G_NONE}};
    while (!todo.empty()) {
        auto [addr, guard] = todo.back();
        todo.pop_back();
        if (addr < 0 || addr >= static_cast<int>(image_.words.size())) continue;
        char& mark = seen[static_cast<size_t>(addr) * 4 + guard];
        if (mark) continue;
        mark = 1;

        const MicroSignals s = fetch(addr);

        // ---- what this micro-operation does ----
        const unsigned unit = s.args >> 7, uop = s.args & 127u;
        if (s.args != 0 && unit == margs::UNIT_ALU) {
            switch (uop) {
                case 1:  c.isAdd = true; break;   case 2:  c.isSub = true; break;
                case 3:  c.isMul = true; break;   case 4:  c.isDiv = true; break;
                case 5:  c.isMod = true; break;   case 6:  c.isCmp = true; break;
                case 7:  c.isAnd = true; break;   case 8:  c.isOR = true; break;
                case 9:  c.isNot = true; break;   case 10: c.isXor = true; break;
                case 11: c.isMov = true; break;   case 12: c.isMovu = true; break;
                case 13: c.isMovh = true; break;  case 14: c.isLsl = true; break;
                case 15: c.isLsr = true; break;   case 16: c.isAsr = true; break;
                default: break;
            }
        }
        if (unit == margs::UNIT_MEM && uop == (margs::LOAD & 127u))  c.isLd = true;
        if (unit == margs::UNIT_MEM && uop == (margs::STORE & 127u)) c.isSt = true;

        if (bit(s.in, MReg::PC)) {                       // pc is loaded
            if (bit(s.out, MReg::BRANCH_TARGET)) {
                if (guard == G_NONE) pcFromTargetUnguarded = true;
                if (guard == G_Z)  beq = true;
                if (guard == G_GT) bgt = true;
                if (guard == G_LT) bsm = true;
            }
            if (bit(s.out, MReg::REG_VAL)) pcFromRegVal = true;
        }
        if (bit(s.in, MReg::REG_SRC) && s.xfer == kXferImm && s.imm == 1 &&
            s.args == margs::WRITE)
            writesRa = true;                             // regSrc <- r1 with a register-file write

        // ---- successors ----
        const uint16_t beginAddr = image_.beginAddr;
        if (s.fetch == kFetchSwitch) continue;
        if (s.fetch == kFetchBranch) {
            if (s.target != beginAddr) todo.push_back({s.target, guard});
            continue;
        }
        if (s.fetch == kFetchCond) {
            int g = guard;
            if (bit(s.out, MReg::FLAG_Z))  g = G_Z;
            if (bit(s.out, MReg::FLAG_GT)) g = G_GT;
            if (bit(s.out, MReg::FLAG_LT)) g = G_LT;
            todo.push_back({s.target, g});               // taken side
        }
        todo.push_back({addr + 1, guard});               // not-taken / sequential side
    }

    // ld / st use the ALU only to form the effective address (base + offset); as in the
    // hardwired decoder that is not an "add" instruction.
    if (c.isLd || c.isSt) c.isAdd = false;

    c.isBeq = beq;  c.isBgt = bgt;  c.isBsm = bsm;
    c.isCall = pcFromTargetUnguarded && writesRa;
    c.isB    = pcFromTargetUnguarded && !writesRa;
    c.isRet  = pcFromRegVal;

    // A routine that does nothing at all is nop.
    const bool anything = c.isAdd || c.isSub || c.isMul || c.isDiv || c.isMod || c.isCmp || c.isAnd || c.isOR ||
                          c.isNot || c.isXor || c.isMov || c.isMovu || c.isMovh || c.isLsl || c.isLsr || c.isAsr ||
                          c.isLd || c.isSt || c.isBeq || c.isBgt || c.isBsm || c.isB || c.isCall || c.isRet;
    c.isNop = !anything;
    return c;
}

void MicroControlUnit::begin(MachineState& machine) {
    if (!loaded_) throw std::logic_error("no microprogram loaded in the control memory");

    machine.reset();                                // registers, memory, flags, pc = 0; instrMemory is kept
    machine.Reg[2] = 4096;                          // same empty, full-descending stack as the pipeline
    dp_ = std::make_unique<MicroDatapath>(machine);

    instructions_ = microInstrs_ = 0;
    ended_ = halted_ = capped_ = false;
    cause_ = 0;

    bubbles_ = fetchStages() - 1;
    cycles_ = bubbles_;                             // the sequencer fills before the first execute

    upc_ = image_.beginAddr;
    active_ = true;
}

bool MicroControlUnit::stepMicro(MicroStep* info) {
    if (!active_ || !dp_) return false;
    MicroDatapath& dp = *dp_;

    if (upc_ < 0 || upc_ >= static_cast<int>(image_.words.size())) throw std::logic_error("micro-pc left the microprogram");

    const int upc = upc_;
    const MicroSignals s = fetch(upc);
    const bool isMBranch = dp.execute(s);
    ++microInstrs_;
    ++cycles_;

    // micro-fetch multiplexer (book Table 8.9)
    int next = upc + 1;
    bool redirect = false;
    bool newInstruction = false;
    const char* mux = "next";
    switch (s.fetch) {
        case kFetchBranch:
            next = s.target;
            redirect = true;
            mux = "branch";
            break;
        case kFetchSwitch: {                        // switch unit: opcode in ir -> start of its microroutine
            const int a = image_.switchAddr[dp.opcode() & 31u];
            if (a >= 0) {
                next = a;
                ++instructions_;
                newInstruction = true;
            } else {
                dp.raise(mcause::ILLEGAL_OPCODE);
            }
            redirect = true;
            mux = "switch";
            break;
        }
        case kFetchCond:
            if (isMBranch) {
                next = s.target;
                redirect = true;
                mux = "cond-taken";
            } else {
                mux = "cond-not-taken";
            }
            break;
        default:
            break;
    }

    bool exception = false;
    if (dp.faultPending()) {                        // a unit raised an exception: enter the exception routine
        dp.clearFault();
        next = image_.exceptionAddr;
        redirect = true;
        exception = true;
        mux = "exception";
    }

    bool finished = false;
    if (dp.halted())       { halted_ = true; cause_ = dp.excCause(); finished = true; }
    else if (dp.programEnded()) { ended_ = true; finished = true; }
    else if (instructions_ >= kMaxInstructions) { capped_ = true; finished = true; }

    if (!finished) {
        if (redirect) cycles_ += bubbles_;
        upc_ = next;
    } else {
        active_ = false;
    }

    if (info) {
        info->upc = upc;
        info->instr = image_.words[static_cast<size_t>(upc)];
        info->text = toText(info->instr);
        info->word = wordText(upc);
        info->signals = s;
        info->isMBranch = isMBranch;
        info->mux = mux;
        info->nextUpc = next;
        info->redirect = redirect;
        info->newInstruction = newInstruction;
        info->exception = exception;
        info->cycles = cycles_;
        info->finished = finished;
    }
    return true;
}

void MicroControlUnit::run(MachineState& machine) {
    begin(machine);
    while (stepMicro()) {}
}

std::string MicroControlUnit::describeAddress(int addr) const {
    if (addr < 0 || addr >= static_cast<int>(image_.words.size())) return "?";
    for (int a = addr; a >= 0; a--) {
        const std::string& l = image_.words[static_cast<size_t>(a)].label;
        if (!l.empty()) return a == addr ? l : l + "+" + std::to_string(addr - a);
    }
    return "+" + std::to_string(addr);
}

std::string MicroControlUnit::listing() const {
    std::ostringstream o;
    o << name() << " microcode: " << wordBits() << "-bit words, " << image_.words.size() << " of "
      << kControlMemWords << " control-memory words used (" << controlStoreBits() << " bits)\n";

    auto pad = [](std::string s, size_t w) { if (s.size() < w) s.append(w - s.size(), ' '); return s; };
    for (size_t i = 0; i < image_.words.size(); i++) {
        std::string addr = std::to_string(i);
        if (addr.size() < 4) addr.insert(0, 4 - addr.size(), '0');
        const MicroInstr& m = image_.words[i];
        o << addr << "  " << pad(m.label, 14) << "  " << pad(toText(m), 42) << "  " << wordText(static_cast<int>(i)) << "\n";
    }
    return o.str();
}

// ---------------------------------------------------------------------------
//  Horizontal: the word holds the control signals themselves
// ---------------------------------------------------------------------------
void HorizontalMicroControlUnit::store(int addr, const MicroInstr& m, uint16_t targetAddr) {
    mem_[addr] = encodeHorizontal(signalsFor(m.op, m.dst, m.src, m.imm, targetAddr, m.args));
}

std::string HorizontalMicroControlUnit::wordText(int addr) const {
    // fields, MSB first: fetch(2) xfer(2) in(11) out(23) | imm(12) | target(10) | args(10)
    const std::string b = mem_[addr].to_string();
    size_t p = 0;
    auto take = [&](size_t n) { std::string s = b.substr(p, n); p += n; return s; };
    std::string r;
    r += take(kFetchBits) + " ";
    r += take(kXferBits) + " ";
    r += take(kNumInSignals) + " ";
    r += take(kNumOutSignals) + " | ";
    r += take(kImmBits) + " ";
    r += take(kTargetBits) + " ";
    r += take(kArgsBits);
    return r;
}

// ---------------------------------------------------------------------------
//  Vertical: the word holds type / src / dest / fields; the micro-decode unit
//  (decodeVertical) produces the control signals when the word is fetched
// ---------------------------------------------------------------------------
void VerticalMicroControlUnit::store(int addr, const MicroInstr& m, uint16_t targetAddr) {
    mem_[addr] = encodeVertical(m, targetAddr);
}

std::string VerticalMicroControlUnit::wordText(int addr) const {
    // fields, MSB first: type(3) src(5) dst(5) imm(12) target(10) args(10)
    const std::string b = mem_[addr].to_string();
    size_t p = 0;
    auto take = [&](size_t n) { std::string s = b.substr(p, n); p += n; return s; };
    std::string r;
    r += take(kTypeBits) + " ";
    r += take(kRegIdBits) + " ";
    r += take(kRegIdBits) + " ";
    r += take(kImmBits) + " ";
    r += take(kTargetBits) + " ";
    r += take(kArgsBits);
    return r;
}