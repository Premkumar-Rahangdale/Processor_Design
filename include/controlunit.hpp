#pragma once
#include <string>

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

class controlunit {
public:
    controlsignals signals{};
    void generatecontrolsignals(const std::string &opcodeandI);

private:
    void reset();
};