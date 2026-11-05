# 4-stage and 6-stage RISC201 pipelines

One cycle-accurate, in-order engine runs both processors. The two differ only in their stage layout.

| File | Role |
|---|---|
| `include/pipeline.hpp`, `src/pipeline.cpp` | the engine (`Pipeline`, `Latch`, hazards, prediction, exceptions) |
| `include/pipeline4stage.hpp`, `src/pipeline4stage.cpp` | `Pipeline4stage`: the 4-stage layout |
| `include/pipeline6stage.hpp`, `src/pipeline6stage.cpp` | `Pipeline6stage`: the 6-stage layout |

| processor | stages | register read | ALU + branch resolve | data memory |
|---|---|---|---|---|
| 4-stage | IF, ID, EX/MEM, WB | ID | EX/MEM | EX/MEM |
| 6-stage | IF, ID, RR, EX, MEM, WB | RR | EX | MEM |

The stage index is the micro-PC of that stage (00..03 for the 4-stage, 00..05 for the 6-stage). `stageLatch(s)` returns the instruction currently in stage `s`.

Interface: `run`, `begin`, `tick`, `step`, `stepN`, `nextPc`, `finished`, `faulted`, `faultCause`, plus the statistics in section 6.

---

## 1. Clocking and start-up

One `tick()` is one clock; all stages work on different instructions. Order inside a clock: WB (commit), ID (control unit), hazard check, stage work oldest-first, resolve control transfers (flush on a misprediction), advance (or stall), IF of the next clock. Ideal cycles = instructions + stages − 1.

`begin()` starts a fresh run: `machine.reset()` (registers, memory, flags; `pc` = the `.main` entry point, or 0), then `r2` (the stack pointer) is set to 4096, an empty full-descending stack. The program in `instrMemory` is kept. Label lines are skipped by IF at no cost.

Every ALU operation, including the divider and the floating-point unit, finishes in one EX clock. There is no multi-cycle execute model.

---

## 2. Data hazards

Only RAW matters in an in-order pipeline: writes happen only in WB and in program order, so WAR and WAW cannot occur.

Sources: `rs1` (ALU ops, `ld`, `st`), `rs2` when I = 0, `rd` for `st`, `r1` for `ret`. Destination: `rd`, and `r1` for `call`. `r0` never causes a hazard.

| mode | rule | back-to-back RAW stalls (6 / 4-stage) |
|---|---|---|
| stall (default) | wait until the producer has left WB (a read in the same clock as the write-back is a hazard) | 3 / 2 |
| stall + `writeThrough` | a read in the clock of the write-back is allowed | 2 / 1 |
| `forwarding` | results are forwarded to the first execution stage from the older instructions; only a load whose data is not read yet stalls | 0 / 0, load-use: 1 / 0 |

The stalled instruction and everything behind it hold; a bubble goes down the pipe.

---

## 3. Control hazards and branch prediction

`beq`, `bgt`, `bsm`, `b`, `call` and `ret` all resolve in EX (EX/MEM). The fetch stage guesses, according to `predictor`:

| predictor | behaviour |
|---|---|
| `NOT_TAKEN` | every control transfer is assumed not taken; a taken one flushes the younger instructions |
| `TWO_BIT` | a branch target buffer (BTB) remembers, per control-transfer instruction, its last target and a 2-bit saturating counter (0, 1 = not taken, 2, 3 = taken). A hit with counter ≥ 2 sends the fetch straight to the target, with no bubble |

A guess is wrong when the direction differs, or when a taken branch was sent to a different target (for example a `ret` with a new return address). A misprediction squashes the younger instructions and restarts the fetch at the real next instruction: the target, or `pc + 1` if a predicted-taken branch is not taken. **Penalty = 3 clocks (6-stage), 2 clocks (4-stage).**

New BTB entries are created when a control transfer is taken (counter starts at 1, becomes 2); not-taken resolutions decrement an existing entry.

`cmp` sets the flags in EX and a following branch sees them in the next clock without a stall. The flags are committed to the machine in WB, so the architectural state stays precise.

CPI = 1 + (mispredictions / instructions) × penalty, plus stalls and control-path bubbles.

---

## 4. Exceptions (precise)

An exception is detected where it occurs and carried down the pipe with the instruction. When the faulting instruction reaches WB, it writes **R4 = cause** and **R5 = its index**, everything younger is squashed (a younger store never reaches memory), the message is printed, and the processor stops. Older instructions have already completed. Faults on a squashed wrong path are ignored.

The stack lives in data words 3072…4095; 4096 is the "empty" stack pointer value.

| Code | Cause | Detected in |
|---|---|---|
| 1 | illegal opcode / bad instruction word | IF (bad word), ID (unknown opcode) |
| 2 | integer division or modulo by zero | EX |
| 3 | data address outside memory | MEM |
| 4 | stack overflow | EX: a write to `r2` below 3072. MEM: a `st` through `r2` outside 3072…4095 |
| 5 | stack underflow | MEM: a `ld` through `r2` at address 4096 |
| 6 | stack pointer corruption | EX: a write to `r2` above 4096 |
| 7 | stack address violation | MEM: a `ld` through `r2` outside 3072…4095 |

"Through `r2`" means the instruction's base register `rs1` is `r2`. These codes and conventions are the same as in the micro control unit (`mcause`, see `docs/Microcode.MD`).

---

## 5. Control unit link

The control signals come from the `controlunit` base class in ID:

```cpp
pipeline.useMicrocode(&horizontalUnit);   // or &verticalUnit
pipeline.useMicrocode(nullptr);           // hardwired decoder
```

ID also sets `isFloat` on an `add`/`sub`/`mul`/`div` in register form whose two sources are float registers (`r26`…`r31`), which makes the ALU use its floating-point unit (see `docs/Alu.MD`).

The vertical control word needs the extra micro-decode stage, so after start-up and after every flush the control path needs one extra clock: `decodePenalty()` is 0 for hardwired and horizontal, 1 for vertical. Those clocks appear in `controlBubbles()`.

---

## 6. Statistics

Iron law: time = instructions × CPI × cycle time.

| Function | Meaning |
|---|---|
| `instructionsExecuted()`, `cycles()`, `cpi()` | totals |
| `dataStallCycles()` | clocks lost to RAW hazards |
| `flushes()`, `squashedInstructions()` | mispredictions that redirected the fetch, and the wrong-path instructions thrown away |
| `branches()`, `mispredictions()`, `predictionAccuracy()` | control transfers resolved and how many were guessed wrong |
| `controlBubbles()` | fetch clocks lost refilling the control path |

With `trace = true`, `run()` prints one line per clock (the instruction index in every stage, marked `STALL`, `FLUSH` or `EXCEPTION`) and a summary.

---

## 7. Command line (P.E.K.K.A.)

The processor is configured once, when it is powered on.

| command | effect |
|---|---|
| `start` | asks for the number of stages (`4` or `6`) and the control unit (`hardwired`, `horizontal` or `vertical`) |
| `load`, `assemble`, `disassemble` | prepare the program |
| `config` | show the configuration |
| `execute` | run the program on the configured pipeline and print instructions, clocks, CPI, data-hazard stalls, branches, mispredictions (and accuracy), squashed instructions and control-path bubbles |
| `debug` | step the same configuration (`step`, `step <n>`, `first <n>`, `registers`, `memory`, `stack`, `state`) |
| `microdebug` | with a horizontal or vertical control unit: step the micro control unit one micro-instruction at a time and show the micro-PC (see `docs/Microcode.MD`) |
| `microcode` | list the control memory of the configured micro control unit |

The CLI always uses forwarding and the 2-bit predictor with a BTB; they are not selectable there. The engine itself still supports stall-only mode, `writeThrough` and predict-not-taken through `forwarding`, `writeThrough` and `predictor` for code that drives `Pipeline` directly.

With a microprogrammed control unit, `execute` also runs the program on the micro control unit's own datapath (one micro-instruction per clock, no pipeline) and compares the final registers, memory and flags with the pipeline run. See `docs/Microcode.MD`.

---

## 8. Building

`PekkaMain.cpp` is a single translation unit. It includes `isa.hpp`, `MachineState.hpp`, the three stack headers, then the sources in this order: `assembler`, `disassembler`, `pipeline6stage.hpp`, `controlunit`, `alu`, `pipeline`, `pipeline4stage`, `pipeline6stage`, the four `micro*` files, `loader`, `debugger`, `pekka`.