# 4-stage and 6-stage RISC201 pipelines

Files: `include/pipeline.hpp` + `src/pipeline.cpp` (the engine), `pipeline4stage.*`, `pipeline6stage.*` (stage layouts).
The old one-instruction-at-a-time `Pipeline6stage` is replaced; its public interface (`run`, `begin`, `step`, `stepN`,
`nextPc`, `finished`, `faulted`, `instructionsExecuted`, `trace`) is unchanged.

| processor | stages | register read | ALU + branch resolve | data memory |
|---|---|---|---|---|
| 4-stage | IF, ID, EX/MEM, WB | ID | EX/MEM | EX/MEM |
| 6-stage | IF, ID, RR, EX, MEM, WB | RR | EX | MEM |

The stage index is the micro-PC of that stage (RISC201.pdf: 00..03 for the 4-stage, 00..05 for the 6-stage);
`stageLatch(s)` returns the instruction currently in stage `s`.

## Clocking
One `tick()` is one clock; all stages work on different instructions. Order inside a clock: WB (commit), ID (control
unit), hazard check, stage work oldest-first, resolve control transfers (flush on a misprediction), advance (or stall),
IF of the next clock. Ideal cycles = instructions + stages - 1.

## Data hazards (lecture: only RAW matters in an in-order pipeline)
Writes happen only in WB and in program order, so WAR and WAW cannot occur.
Sources: rs1 (ALU ops, ld, st), rs2 when I = 0, rd for `st`, r1 for `ret`. Destination: rd, r1 for `call`. r0 never causes a hazard.

| mode | rule | back-to-back RAW stalls (6 / 4-stage) |
|---|---|---|
| stall (default) | wait until the producer has left WB (slides: a read in the same clock as the write-back is a hazard) | 3 / 2 |
| stall + `writeThrough` | a read in the clock of the write-back is allowed | 2 / 1 |
| `forwarding` | results are forwarded to the first execution stage from the older instructions; only a load whose data is not read yet stalls | 0 / 0, load-use: 1 / 0 |

The stalled instruction and everything behind it hold, a bubble goes down the pipe.

## Control hazards and branch prediction
`beq/bgt/bsm/b/call/ret` all resolve in EX (EX/MEM). The fetch stage guesses (`predictor`):

| predictor | behaviour |
|---|---|
| `NOT_TAKEN` (default) | every control transfer is assumed not taken; a taken one flushes the younger instructions |
| `TWO_BIT` | a branch target buffer remembers, per control-transfer instruction, its last target and a 2-bit saturating counter (0,1 = not taken, 2,3 = taken). A hit with counter >= 2 sends the fetch straight to the target (no bubble). |

A guess is wrong when the direction differs or a taken branch was sent to another target (e.g. a `ret` with a new
return address). A misprediction squashes the younger instructions and restarts the fetch at the real next instruction
(the target, or pc + 1 if a predicted-taken branch is not taken). Penalty = 3 clocks (6-stage), 2 clocks (4-stage).
New entries are created when a control transfer is taken (counter 1 -> 2); not-taken resolutions decrement an existing entry.
`cmp` sets the flags in EX, a following branch sees them in the next clock without a stall; the flags are committed to
the machine in WB (precise state).

Statistics: `branches()`, `mispredictions()`, `predictionAccuracy()`. CPI = 1 + (mispredictions / instructions) x penalty (+ stalls).

## Exceptions (precise)
Detected where they occur (IF bad word / ID illegal opcode: cause 1, EX division by zero: 2, MEM bad address: 3) and
carried down the pipe. In WB the faulting instruction writes R4 = cause, R5 = its index, squashes everything younger
(a younger store never reaches memory) and stops the processor. Faults on a squashed wrong path are ignored.
Same convention and codes as the micro control unit.

## Control unit link
`useMicrocode(&horizontalUnit)`, `useMicrocode(&verticalUnit)` or `useMicrocode(nullptr)` (hardwired). The control
signals come from the chosen unit in ID. The vertical control word needs the extra micro-decode stage, so after start-up
and after every flush the control path needs one extra clock (`decodePenalty()`: 0 hardwired / horizontal, 1 vertical).

## Statistics (iron law: time = instructions x CPI x cycle time)
`instructionsExecuted()`, `cycles()`, `cpi()`, `dataStallCycles()`, `flushes()`, `squashedInstructions()`,
`controlBubbles()`, `branches()`, `mispredictions()`, `predictionAccuracy()`. With the trace flag, `run()` prints one line per clock.

## CLI (P.E.K.K.A.)
| command | effect |
|---|---|
| `pipeline <4\|6>` | number of pipeline stages |
| `control <hardwired\|horizontal\|vertical>` | control unit (the micro units are loaded with the RISC201 microprogram at start-up) |
| `forwarding <on\|off>` | stall only / forwarding |
| `predictor <none\|2bit>` | predict-not-taken / 2-bit predictor with BTB |
| `config` | show the configuration |

`execute` runs the loaded program on that configuration and prints instructions, clocks, CPI, stalls, branches,
mispredictions, squashed instructions and control-path bubbles. `debug` steps the same configuration.

## Building
`PekkaMain.cpp` includes `src/pipeline.cpp`, `src/pipeline4stage.cpp` and `src/pipeline6stage.cpp` (in that order) and the four `src/micro*.cpp` files.
