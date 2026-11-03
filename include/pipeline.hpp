#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "MachineState.hpp"
#include "controlunit.hpp"
#include "alu.hpp"

// ===========================================================================
//  Cycle-accurate in-order pipeline (common engine of the 4-stage and the
//  6-stage RISC201 processors).
//
//    4-stage : IF  ID(+register read)  EX/MEM  WB
//    6-stage : IF  ID  RR  EX  MEM  WB
//
//  Every clock all stages work on different instructions. The engine
//    * detects RAW data hazards and resolves them by STALLING (default) or by
//      FORWARDING (optional, `forwarding`), a stall inserts a bubble,
//    * predicts control transfers at fetch (not-taken, or a 2-bit predictor with a
//      branch target buffer) and FLUSHES the younger instructions when a prediction
//      turns out wrong (all control transfers resolve in EX),
//    * raises PRECISE exceptions: the faulting instruction is carried down the
//      pipe, it takes effect when it reaches WB, older instructions have then
//      completed and younger ones are squashed (R4 = cause, R5 = index of the
//      faulting instruction, then the processor stops).
//    * can PREDICT control transfers (optional, `predictor`): a branch target buffer
//      (BTB) with a 2-bit saturating counter per branch is looked up in IF; a taken
//      prediction redirects the fetch at once, a misprediction is found in EX and
//      flushes the younger instructions (lecture: Pipelines-2, branch prediction).
//  The control signals come from `controlunit` (hardwired or one of the
//  microprogrammed units, see controlunit::useMicrocode()).
// ===========================================================================

// Exception causes (same codes as mcause:: in microinstr.hpp)
namespace pcause {
    constexpr int NONE = 0;
    constexpr int ILLEGAL_OPCODE = 1;
    constexpr int DIV_BY_ZERO = 2;
    constexpr int MEM_ADDRESS = 3;
    constexpr int STACK_OVERFLOW = 4;
    constexpr int STACK_UNDERFLOW = 5;
    constexpr int STACK_POINTER_CORRUPTION = 6;
    constexpr int STACK_ADDRESS_VIOLATION = 7;
}

// The contents of one pipeline stage (the pipeline register in front of it).
struct Latch {
    bool valid = false;          // false = bubble
    int  pc = 0;                 // index of this instruction in instrMemory
    std::string raw;             // 32-char binary string

    controlsignals sig;          // filled in ID
    bool decoded = false;

    int rd = 0, rs1 = 0, rs2 = 0;
    int imm = 0;                 // sign-extended 16-bit immediate
    int offset = 0;              // sign-extended 27-bit branch offset
    int dest = 0;                // register this instruction writes (0 = none)

    int opA = 0, opB = 0;        // operands read in the register-read stage
    int storeVal = 0;            // value to store (st)

    int aluResult = 0;           // ALU result / ld-st address / call link
    int memData = 0;             // loaded value
    int writeData = 0;           // final value to write back
    bool resultReady = false;    // writeData is final (usable for forwarding)

    bool branchTaken = false;    // resolved direction (control-transfer instructions)
    int  branchTarget = 0;       // resolved target (also set when not taken, for the target buffer)

    bool setsFlags = false;      // cmp: flags are committed in WB
    bool fz = false, fn = false, fo = false, fc = false;

    bool predTaken = false;      // the predictor sent the fetch to predTarget
    int  predTarget = 0;

    int fault = pcause::NONE;    // exception detected by this instruction
    std::string faultMsg;
};

// Branch prediction scheme
//   NOT_TAKEN : no predictor, every control transfer is assumed not taken (static)
//   TWO_BIT   : BTB + 2-bit saturating counter per branch (0,1 = not taken, 2,3 = taken)
enum class Predictor { NOT_TAKEN, TWO_BIT };

class Pipeline : public controlunit
{
public:
    enum class Stage { IF, ID, RR, EX, MEM, EXMEM, WB };

    // ---- configuration (set before begin()/run()) ----
    bool trace = false;          // print one line per clock + a summary at the end of run()
    bool forwarding = false;     // false: stall on every RAW hazard, true: forward results
    bool writeThrough = false;   // stall-only mode: a register written in WB can be read in the same clock
    Predictor predictor = Predictor::NOT_TAKEN;   // branch prediction scheme

    virtual ~Pipeline() = default;
    virtual const char* name() const = 0;

    // ---- running ----
    void run(MachineState& machine);

    // begin()   : fresh start (machine.reset(): registers, memory, flags, pc; instrMemory is kept)
    // tick()    : one clock; false when there was nothing left to do
    // step()    : clocks until ONE instruction has completed (label lines are skipped); false if
    //             nothing completed because the program is finished or an exception stopped it
    // stepN()   : up to n instructions; the index of every completed instruction is appended to
    //             executedPcs (if given); returns how many completed
    // nextPc()  : index of the oldest instruction that has not completed yet, -1 if none
    // finished(): nothing left to execute
    void begin(MachineState& machine);
    bool tick(MachineState& machine);
    bool step(MachineState& machine);
    int  stepN(MachineState& machine, int n, std::vector<int>* executedPcs = nullptr);
    int  nextPc(const MachineState& machine) const;
    bool finished(const MachineState& machine) const;

    // ---- results ----
    bool faulted() const { return halted_; }               // stopped by an exception
    int  faultCause() const { return faultCause_; }        // pcause::*, also left in R4
    long instructionsExecuted() const { return executed_; }
    long cycles() const { return cycles_; }
    long dataStallCycles() const { return dataStalls_; }   // clocks lost to RAW hazards
    long flushes() const { return flushes_; }              // redirects (= mispredicted control transfers)
    long branches() const { return branches_; }            // control transfers resolved (b, beq, bgt, bsm, call, ret)
    long mispredictions() const { return mispredicts_; }
    double predictionAccuracy() const { return branches_ ? 1.0 - double(mispredicts_) / double(branches_) : 1.0; }
    long squashedInstructions() const { return squashed_; }// wrong-path instructions thrown away
    long controlBubbles() const { return ctlBubbles_; }    // fetch clocks lost refilling the control path
    double cpi() const { return executed_ ? double(cycles_) / double(executed_) : 0.0; }

    // ---- structure (for displays / evaluation) ----
    int stageCount() const { return static_cast<int>(layout_.size()); }
    int resolveStage() const { return readIdx_ + 1; }      // stage index where branches resolve (EX / EX/MEM)
    const std::string& stageName(int s) const { return names_[s]; }
    const Latch& stageLatch(int s) const { return slot_[s]; }   // stage index == the micro-PC of the stage

    static constexpr long kMaxInstructions = 1000000;      // runaway-loop guard
    static constexpr long kMaxCycles = 20000000;

protected:
    Pipeline(std::vector<Stage> layout, std::vector<std::string> names);

private:
    static constexpr int kRa = 1;                          // "ra" is r1

    std::vector<Stage> layout_;
    std::vector<std::string> names_;
    std::vector<Latch> slot_;                              // slot_[s] = instruction in stage s
    int idIdx_ = 1, readIdx_ = 1, memIdx_ = 2;             // ID, register-read stage, stage that finishes a load

    bool halted_ = false;
    int  faultCause_ = 0;
    long executed_ = 0, cycles_ = 0, dataStalls_ = 0, flushes_ = 0, squashed_ = 0, ctlBubbles_ = 0;
    long branches_ = 0, mispredicts_ = 0;
    struct BtbEntry { int target = 0; int counter = 1; };   // counter: 2-bit saturating, new entries start weakly not taken
    std::unordered_map<int, BtbEntry> btb_;                 // branch index -> target + counter
    int  fetchDelay_ = 0;                                  // IF bubbles still to insert (control-path refill)
    int  lastRetiredPc_ = -1;
    bool sFz_ = false, sFn_ = false, sFo_ = false, sFc_ = false;   // flags as seen by branches in EX
    ALU alu_unit;

    bool fetchInto(MachineState& m, Latch& out);
    void prefetch(MachineState& m);
    bool fetchHasWork(const MachineState& m) const;
    void decode(Latch& L);
    bool hasHazard(const Latch& consumer, const std::vector<Latch>& snap) const;
    void readOperands(MachineState& m, Latch& L);
    void forwardOperands(Latch& L, const std::vector<Latch>& snap, int stageIdx) const;
    void exStage(MachineState& m, Latch& L, const std::vector<Latch>& snap, int stageIdx);
    void memStage(MachineState& m, Latch& L);
    void retire(MachineState& m, Latch& L);
    bool resolveBranch(Latch& L, int& target);               // true = misprediction: redirect to target
    void takeException(MachineState& m, const Latch& faulting);
    void traceLine(const std::vector<Latch>& snap, bool stall, bool redirected) const;
    static void sources(const Latch& L, int out[4], int& n);

    static int field(const std::string& s, int start, int len);
    static int signExtend(int v, int bits);
};
