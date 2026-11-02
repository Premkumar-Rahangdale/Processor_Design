#pragma once
#include "pipeline.hpp"

// 6-stage RISC201: IF  ID  RR  EX  MEM  WB
//   register read in RR, ALU / branch resolution in EX, data memory in MEM.
// All behaviour (run, begin, step, stepN, nextPc, finished, statistics, hazards) is in Pipeline.
class Pipeline6stage : public Pipeline
{
public:
    Pipeline6stage();
    const char* name() const override { return "6-stage"; }
};
