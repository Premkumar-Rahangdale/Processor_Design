#include "../include/controlunit.hpp"
#include <bitset>

void controlunit::reset() {
    signals = controlsignals{};
}

void controlunit::generatecontrolsignals(const std::string &opcodeandI) {
    reset();
    std::bitset<5> opcode;
    for(int i=0;i<5;i++){
        opcode[i]=(opcodeandI[i]-'0');
    }
    signals.isImm = opcodeandI[5]-'0';
    
    signals.isAdd  = ~opcode[0] & ~opcode[1] & ~opcode[2] & ~opcode[3] &  opcode[4]; // 00001
    signals.isSub  = ~opcode[0] & ~opcode[1] & ~opcode[2] &  opcode[3] & ~opcode[4]; // 00010
    signals.isMul  = ~opcode[0] & ~opcode[1] & ~opcode[2] &  opcode[3] &  opcode[4]; // 00011
    signals.isDiv  = ~opcode[0] & ~opcode[1] &  opcode[2] & ~opcode[3] & ~opcode[4]; // 00100
    signals.isMod  = ~opcode[0] & ~opcode[1] &  opcode[2] & ~opcode[3] &  opcode[4]; // 00101
    signals.isCmp  = ~opcode[0] & ~opcode[1] &  opcode[2] &  opcode[3] & ~opcode[4]; // 00110
    signals.isAnd  = ~opcode[0] & ~opcode[1] &  opcode[2] &  opcode[3] &  opcode[4]; // 00111
    signals.isOR   = ~opcode[0] &  opcode[1] & ~opcode[2] & ~opcode[3] & ~opcode[4]; // 01000
    signals.isNot  = ~opcode[0] &  opcode[1] & ~opcode[2] & ~opcode[3] &  opcode[4]; // 01001
    signals.isXor  = ~opcode[0] &  opcode[1] & ~opcode[2] &  opcode[3] & ~opcode[4]; // 01010
    signals.isMov  = ~opcode[0] &  opcode[1] & ~opcode[2] &  opcode[3] &  opcode[4]; // 01011
    signals.isMovu = ~opcode[0] &  opcode[1] &  opcode[2] & ~opcode[3] & ~opcode[4]; // 01100
    signals.isMovh = ~opcode[0] &  opcode[1] &  opcode[2] & ~opcode[3] &  opcode[4]; // 01101
    signals.isLsl  = ~opcode[0] &  opcode[1] &  opcode[2] &  opcode[3] & ~opcode[4]; // 01110
    signals.isLsr  = ~opcode[0] &  opcode[1] &  opcode[2] &  opcode[3] &  opcode[4]; // 01111
    signals.isAsr  =  opcode[0] & ~opcode[1] & ~opcode[2] & ~opcode[3] & ~opcode[4]; // 10000
    signals.isNop  =  opcode[0] & ~opcode[1] & ~opcode[2] & ~opcode[3] &  opcode[4]; // 10001
    signals.isLd   =  opcode[0] & ~opcode[1] & ~opcode[2] &  opcode[3] & ~opcode[4]; // 10010
    signals.isSt   =  opcode[0] & ~opcode[1] & ~opcode[2] &  opcode[3] &  opcode[4]; // 10011
    signals.isBeq  =  opcode[0] & ~opcode[1] &  opcode[2] & ~opcode[3] & ~opcode[4]; // 10100
    signals.isBgt  =  opcode[0] & ~opcode[1] &  opcode[2] & ~opcode[3] &  opcode[4]; // 10101
    signals.isBsm  =  opcode[0] & ~opcode[1] &  opcode[2] &  opcode[3] & ~opcode[4]; // 10110
    signals.isB    =  opcode[0] & ~opcode[1] &  opcode[2] &  opcode[3] &  opcode[4]; // 10111
    signals.isCall =  opcode[0] &  opcode[1] & ~opcode[2] & ~opcode[3] & ~opcode[4]; // 11000
    signals.isRet  =  opcode[0] &  opcode[1] & ~opcode[2] & ~opcode[3] &  opcode[4]; // 11001
}