#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "MachineState.hpp"
#include "controlunit.hpp"
#include "microinstr.hpp"
#include "microcode.hpp"
#include "microdatapath.hpp"

// ===========================================================================
//  Micro-control units (book section 8.8)
//
//  Common part (this base class): micro-PC, micro-fetch multiplexer, switch
//  unit, exception entry, run loop, cycle accounting.
//  Format-specific part (derived classes): how a microinstruction is stored in
//  the control memory and how it becomes control signals.
//
//    HorizontalMicroControlUnit : 70-bit words, signals stored in the word,
//                                 no decode stage        (fetch -> execute)
//    VerticalMicroControlUnit   : 45-bit words, signals produced by the
//                                 micro-decode unit      (fetch -> decode -> execute)
//
//  Cycle model (pipelined micro-sequencer): one micro-instruction executes per
//  cycle; whenever the micro-fetch multiplexer selects anything but "micro-pc
//  + 1" (mb, a taken mbeq, mswitch, an exception entry) the stages in front of
//  execute are flushed: 1 bubble for horizontal, 2 for vertical. Starting up
//  costs the same number of cycles.
// ===========================================================================
class MicroControlUnit {
public:
    static constexpr long kMaxInstructions = 1000000;    // runaway guard, same as Pipeline6stage

    virtual ~MicroControlUnit() = default;

    // Micro-assemble the program and write it into the control memory. Can be
    // called again at any time to replace the microcode.
    void load(const MicroProgram& prog);

    // Reset the machine (instrMemory is kept) and run the program in
    // instrMemory to its end, an exception, or the instruction limit.
    void run(MachineState& machine);

    // ---- results of the last run ----
    long instructionsExecuted() const { return instructions_; }
    long microInstructionsExecuted() const { return microInstrs_; }
    long cycles() const { return cycles_; }
    bool finishedNormally() const { return ended_; }     // pc left the program
    bool faulted() const { return halted_; }             // stopped by the exception routine
    uint32_t faultCause() const { return cause_; }       // mcause::*, also left in R4
    bool hitInstructionLimit() const { return capped_; }

    // ---- decoder interface used by controlunit (hardwired <-> microprogrammed) ----
    // Control signals of the instruction whose first 6 bits (opcode + I) are given.
    // They are read out of the control memory: the microroutine the switch unit
    // selects for this opcode is walked and what it does is turned into the
    // controlsignals flags. An opcode without a microroutine gives all-false
    // signals (isKnown() == false). The table is rebuilt by every load().
    controlsignals controlSignalsFor(const std::string& opcodeAndI) const;

    // ---- static properties ----
    virtual const char* name() const = 0;
    virtual int wordBits() const = 0;                    // width of one control-memory word
    virtual int fetchStages() const = 0;                 // stages before execute + 1
    int wordsUsed() const { return static_cast<int>(image_.words.size()); }
    // Clocks lost refilling the control path after a redirect: stages in front of execute minus the
    // fetch itself. Horizontal 0, vertical 1 (the micro-decode stage).
    int decodePenalty() const { return fetchStages() - 2; }
    long controlStoreBits() const { return static_cast<long>(wordsUsed()) * wordBits(); }

    // Control-store listing: address, label, microassembly, encoded word.
    // Builds the text only; nothing is printed.
    std::string listing() const;

protected:
    virtual void store(int addr, const MicroInstr& m, uint16_t targetAddr) = 0;
    virtual MicroSignals fetch(int addr) const = 0;      // control memory -> signals
    virtual std::string wordText(int addr) const = 0;    // encoded word, binary, grouped by field
    virtual void resizeMemory(int words) = 0;

private:
    controlsignals analyseRoutine(int startAddr) const;   // walk one microroutine
    void buildDecodeTable();

    std::array<controlsignals, 32> decode_{};            // indexed by opcode, isImm filled in per call
    AssembledMicroProgram image_;
    bool loaded_ = false;
    long instructions_ = 0, microInstrs_ = 0, cycles_ = 0;
    bool ended_ = false, halted_ = false, capped_ = false;
    uint32_t cause_ = 0;
};

// ---------------------------------------------------------------------------
class HorizontalMicroControlUnit : public MicroControlUnit {
public:
    const char* name() const override { return "horizontal"; }
    int wordBits() const override { return kHorizontalBits; }
    int fetchStages() const override { return 2; }       // fetch, execute

protected:
    void store(int addr, const MicroInstr& m, uint16_t targetAddr) override;
    MicroSignals fetch(int addr) const override { return unpackHorizontal(mem_[addr]); }
    std::string wordText(int addr) const override;
    void resizeMemory(int words) override { mem_.assign(words, HorizontalWord()); }

private:
    std::vector<HorizontalWord> mem_;
};

// ---------------------------------------------------------------------------
class VerticalMicroControlUnit : public MicroControlUnit {
public:
    const char* name() const override { return "vertical"; }
    int wordBits() const override { return kVerticalBits; }
    int fetchStages() const override { return 3; }       // fetch, micro-decode, execute

protected:
    void store(int addr, const MicroInstr& m, uint16_t targetAddr) override;
    MicroSignals fetch(int addr) const override { return decodeVertical(mem_[addr]); }
    std::string wordText(int addr) const override;
    void resizeMemory(int words) override { mem_.assign(words, VerticalWord()); }

private:
    std::vector<VerticalWord> mem_;
};