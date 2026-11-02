#include "../include/pipeline6stage.hpp"

Pipeline6stage::Pipeline6stage()
    : Pipeline({Stage::IF, Stage::ID, Stage::RR, Stage::EX, Stage::MEM, Stage::WB},
               {"IF", "ID", "RR", "EX", "MEM", "WB"}) {}
