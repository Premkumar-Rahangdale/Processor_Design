#pragma once
#include <string>

class MicroControlUnit;   // microprogrammed control unit (include/microcontrolunit.hpp)

// The control signals of one instruction. They are produced by the control unit
// in the ID stage and travel with the instruction through the pipeline.
struct controlsignals{
    bool isImm=false;
    bool isAdd=false;
    bool isSub=false;
    bool isMul=false;
    bool isDiv=false;
    bool isMod=false;
    bool isCmp=false;
    bool isAnd=false;
    bool isOR=false;
    bool isNot=false;
    bool isXor=false;
    bool isMov=false;
    bool isMovu=false;
    bool isMovh=false;
    bool isLsl=false;
    bool isLsr=false;
    bool isAsr=false;
    bool isNop=false;
    bool isLd=false;
    bool isSt=false;
    bool isBeq=false;
    bool isBgt=false;
    bool isBsm=false;
    bool isB=false;
    bool isCall=false;
    bool isRet=false;


    bool readsRs1() const {
        return isAdd || isSub || isMul || isDiv || isMod || isCmp || isAnd || isOR || isXor || isLsl || isLsr || isAsr || isLd || isSt;
    }

    bool usesOperandB() const {
        return readsRs1() || isNot || isMov || isMovu || isMovh;
    }

    bool usesOffset() const {
        return isBeq || isBgt || isBsm || isB || isCall;
    }

    bool writesReg() const {
        return isAdd || isSub || isMul || isDiv || isMod || isAnd || isOR || isNot || isXor || isMov || isMovu || isMovh || isLsl || isLsr || isAsr || isLd || isCall;
    }

    bool isKnown() const {
        return readsRs1() || isNot || isMov || isMovu || isMovh || isNop || usesOffset() || isRet;
    }
};

// Control unit of the processor. The interface is the same for both designs:
//   input  : opcode + I bit of the instruction (first 6 characters of the word)
//   output : `signals`
//
//   hardwired (default)  : the signals come from combinational decode logic
//   microprogrammed      : the signals are read out of the control memory of a
//                          MicroControlUnit (horizontal or vertical), see useMicrocode()
//
// Pipeline6stage derives from this class, so a pipeline selects its control unit with
//     pipeline.useMicrocode(&horizontalUnit);   // or &verticalUnit
//     pipeline.useMicrocode(nullptr);           // back to hardwired
// The microprogrammed unit is not owned: it must outlive its use and have a microprogram loaded.
class controlunit {
public:
    controlsignals signals{};
    void generatecontrolsignals(const std::string &opcodeandI);

    void useMicrocode(const MicroControlUnit *unit) { micro = unit; }
    bool usesMicrocode() const { return micro != nullptr; }

    // Extra clocks the control path needs to refill after the instruction flow is redirected (and at
    // start-up): 0 for the hardwired and the horizontal unit, 1 for the vertical unit (micro-decode stage).
    int decodePenalty() const;

private:
    const MicroControlUnit *micro = nullptr;   // nullptr = hardwired decoder

    void reset();
    void decodeHardwired(const std::string &opcodeandI);
};
