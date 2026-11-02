#include "../include/pipeline4stage.hpp"

Pipeline4stage::Pipeline4stage()
    : Pipeline({Stage::IF, Stage::ID, Stage::EXMEM, Stage::WB},
               {"IF", "ID", "EX/MEM", "WB"}) {}
