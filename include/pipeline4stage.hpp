#pragma once
#include "pipeline.hpp"

// 4-stage RISC201: IF  ID  EX/MEM  WB
//   ID decodes and reads the register file, EX/MEM does the ALU work, the branch decision and the
//   data-memory access in one stage.
// All behaviour (run, begin, step, stepN, nextPc, finished, statistics, hazards) is in Pipeline.
class Pipeline4stage : public Pipeline
{
public:
    Pipeline4stage();
    const char* name() const override { return "4-stage"; }
};
