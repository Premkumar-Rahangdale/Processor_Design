# Microprogrammed control unit: design, structure and execution

A microprogrammed control unit for the RISC201 simulator, in two versions: **horizontal** and **vertical**. Both follow the book / lecture design (Computer Organisation and Architecture, Chapter 8): a bus-based data path, 8 microinstructions, a microprogram per instruction stored in a **control memory**, and a micro-PC that steps through it. They run the same microprogram and differ only in how a microinstruction is encoded and fetched.

> **Current state:** the two units execute whole instructions one at a time on their own data path (the same sequential model as `Pipeline6stage::run()` today). They are not connected to the pipeline yet. When the 4- and 6-stage pipelines are made to overlap, the control words produced here are what will feed them.

| File | Role |
|---|---|
| `include/microinstr.hpp`, `src/microinstr.cpp` | micro-registers, the 8 microinstructions, `args` encoding, both word formats, encode / decode |
| `include/microdatapath.hpp`, `src/microdatapath.cpp` | the data path: decode unit, register file, ALU, data memory, exception unit, shared bus |
| `include/microcode.hpp`, `src/microcode.cpp` | the RISC201 microprogram (symbolic tables) and the micro-assembler |
| `include/microcontrolunit.hpp`, `src/microcontrolunit.cpp` | `HorizontalMicroControlUnit`, `VerticalMicroControlUnit` and their common base |

Nothing existing is modified. The data path uses `MachineState` and the existing `ALU` class as they are.

---

## 1. Data path

All units talk through **micro-registers** and one shared bus. A cycle works like this (book section 8.7):

1. The register chosen by an *out* signal drives the **write bus** (at most one).
2. The **transfer multiplexer** picks the write bus, the write bus + `μimm` (the micro-adder), or `μimm`. This is the **read bus**.
3. Every register with its *in* signal set loads the read bus.
4. The unit named by `args` performs its operation using the registers as they are after step 3. Results appear in the output registers (`regVal`, `aluResult`, `ldResult`, flags) and are usable from the next microinstruction.

`mbeq` compares the write bus with `μimm` (the `isMBranch` signal).

### Micro-registers (24)

| Register | Bits | Bus | Meaning |
|---|---|---|---|
| `pc` | 32 | both | the machine's `pc`, an index into `instrMemory` |
| `ir` | 32 | in (loaded by `mloadIR`) | instruction register |
| `I`, `rd`, `rs1`, `rs2` | 1, 5, 5, 5 | out | decoded fields |
| `immx` | 32 | out | sign-extended 16-bit immediate |
| `branchTarget` | 32 | out | `pc` at decode time + sign-extended 27-bit offset |
| `regSrc`, `regData`, `regVal` | 5, 32, 32 | both | register-file address / write data / read data |
| `A`, `B`, `aluResult` | 32 | both | ALU operands and result |
| `flags.Z`, `.N`, `.O`, `.C` | 1 | out | set by `cmp` |
| `flags.GT`, `flags.LT` | 1 | out | derived on `cmp`: GT = `!Z && N == O`, LT = `N != O` |
| `mar`, `mdr`, `ldResult` | 32 | both | data-memory address / write data / read data |
| `excCause` | 3 | out | set by hardware when an exception is raised |

Differences from the book: register ids are 5 bits (32 registers), `ra` = r1 and `sp` = r2, immediates have no `u`/`h` modifiers (`movu` / `movh` are ALU operations), and the flags are Z/N/O/C plus GT/LT instead of E/GT.

### Units

| Unit | Behaviour |
|---|---|
| Instruction memory | `mloadIR` loads `ir` from `instrMemory[pc]`. Label entries (`.loop:`) are stepped over at no cost, as the IF stage does. A malformed entry reads as 0 (illegal opcode). When `pc` leaves the program the run ends normally. |
| Decode unit | one cycle after `ir` is loaded (the `mdecode` cycle) it latches `I`, `rd`, `rs1`, `rs2`, `immx`, `branchTarget`. |
| Register file | `<read>`: `regVal = Reg[regSrc]`. `<write>`: `Reg[regSrc] = regData`. R0 always reads 0 and writes to it are ignored. |
| ALU | wraps the existing `ALU` class. Only `<cmp>` updates the flags (also copied to `MachineState`). Division by zero raises an exception. |
| Data memory | `<load>`: `ldResult = memory[mar]`. `<store>`: `memory[mar] = mdr`. A word index outside memory raises an exception and the access does not happen. |
| Exception unit | `<halt>` stops the machine. |

---

## 2. Microinstructions and `args`

| # | Microinstruction | Semantics |
|---|---|---|
| 0 | `mloadIR` | `ir <- [pc]` |
| 1 | `mdecode` | wait one cycle, decode registers get populated |
| 2 | `mswitch` | micro-PC <- start of the routine for the opcode in `ir` |
| 3 | `mmov dst, src [, args]` | `dst <- src` |
| 4 | `mmovi dst, imm [, args]` | `dst <- imm` |
| 5 | `madd dst, imm [, args]` | `dst <- dst + imm` |
| 6 | `mbeq src, imm, label` | if `src == imm`, micro-PC <- label |
| 7 | `mb label` | micro-PC <- label |

`args` is 10 bits: **3-bit unit id | 7-bit unit opcode**, all zero = no operation.

| Unit | Id | Opcodes |
|---|---|---|
| ALU | 000 | 1 add, 2 sub, 3 mul, 4 div, 5 mod, 6 cmp, 7 and, 8 or, 9 not, 10 xor, 11 mov, 12 movu, 13 movh, 14 lsl, 15 lsr, 16 asr (the ISA opcode of the instruction; `ld`/`st` use add) |
| Register file | 001 | 1 read, 2 write |
| Data memory | 010 | 1 load, 2 store |
| Exception unit | 011 | 1 halt |

---

## 3. The two word formats

Same microprogram, two encodings. Widths were recomputed for RISC201.

### Vertical: 45 bits

```
| type 3 | src 5 | dst 5 | imm 12 | branchTarget 10 | args 10 |
```

The word is compact, but a **micro-decode unit** (`decodeVertical`) must turn it into control signals before it executes. `type` takes the 8 microinstructions, register ids take 5 bits (24 registers fit in 32).

### Horizontal: 70 bits

```
| control signals 38 | imm 12 | branchTarget 10 | args 10 |
```

The 38 control signals are stored in the word, so no decode stage is needed:

| Signals | Count | |
|---|---|---|
| out (drive the write bus) | 23 | every register except `ir` |
| in (load from the read bus) | 11 | `pc`, `ir`, `regSrc`, `regData`, `regVal`, `A`, `B`, `aluResult`, `mar`, `mdr`, `ldResult` |
| transfer multiplexer select | 2 | bus / micro-adder / `μimm` |
| micro-fetch multiplexer select | 2 | next / branch / switch / conditional |

For comparison, the book's SimpleRisc design needs 33 signals (65-bit word); RISC201 has 4 more flag registers and `excCause`, hence 38 (70-bit word). The vertical word stays at 45 bits.

### Micro-fetch multiplexer (book Table 8.9)

| Microinstruction | Next micro-PC |
|---|---|
| `mloadIR`, `mdecode`, `mmov`, `mmovi`, `madd` | micro-PC + 1 |
| `mb` | branch target |
| `mbeq` | branch target if `isMBranch`, else micro-PC + 1 |
| `mswitch` | output of the switch unit (opcode -> routine address; illegal opcode -> exception) |

### Control memory

1024 words (10-bit micro-PC). The RISC201 microprogram uses **196** words: 13,720 bits horizontal versus 8,820 bits vertical (horizontal is 56 % larger).

---

## 4. The microprogram

Every instruction starts with the **preamble** and ends with `mb .begin`:

```
.begin:  mloadIR
         mdecode
         madd pc, 1
         mswitch
```

`pc` counts instructions (+1), like the rest of the simulator. After the preamble `pc` already holds the index of the next instruction.

| Class | Routine (condensed) |
|---|---|
| `add sub mul div mod and or xor lsl lsr asr` | read `rs1` into `A`; if `I` read `rs2` else use `immx` into `B` and start the ALU; `Reg[rd] <- aluResult` |
| `mov movu movh not` | the same without `rs1`: `B <- rs2 / immx` with the ALU operation, then write `rd` |
| `cmp` | `A <- Reg[rs1]`, `B <- rs2 / immx` with `<cmp>`; no write-back |
| `ld rd, imm[rs1]` | `A <- Reg[rs1]`, `B <- immx` with `<add>`, `mar <- aluResult` with `<load>`, write `ldResult` to `rd` |
| `st rd, imm[rs1]` | the same address, then `mdr <- Reg[rd]` with `<store>` |
| `beq` / `bgt` / `bsm` | `mbeq flags.Z / flags.GT / flags.LT, 1`; taken: `pc <- branchTarget` |
| `b` | `pc <- branchTarget` |
| `call` | `regData <- pc` (the return address), write to r1, `pc <- branchTarget` |
| `ret` | read r1, `pc <- regVal` |
| `nop` | `mb .begin` |

Number of microinstructions executed per instruction, preamble included:

| Instruction | Count | Instruction | Count |
|---|---|---|---|
| ALU, register form | 13 | `ld`, `st` | 11 |
| ALU, immediate form | 11 | `b` | 6 |
| `mov`-class, register / immediate | 11 / 9 | `beq`/`bgt`/`bsm`, not taken / taken | 6 / 7 |
| `cmp`, register / immediate | 10 / 9 | `call` / `ret` | 8 / 7 |
| `nop` | 5 | | |

### Why branch tests are single `mbeq`s

`mbeq` can only compare one register with an immediate. `bgt` (`!Z && N == O`) and `bsm` (`N != O`) are not single flags, so `cmp` also latches the derived `flags.GT` and `flags.LT`. All three branches are then one `mbeq`, as in the book, and `N`, `O`, `C` stay available for the exception framework.

---

## 5. Cycle model (pipelined micro-sequencer)

One microinstruction executes per cycle. Whenever the micro-fetch multiplexer selects anything but "micro-PC + 1" (`mb`, a taken `mbeq`, `mswitch`, an exception entry), the stages in front of execute are flushed.

| | Horizontal | Vertical |
|---|---|---|
| Stages | fetch, execute | fetch, micro-decode, execute |
| Bubble per redirect / start-up | 1 | 2 |

`cycles = (stages - 1) + microinstructions + (stages - 1) x redirects`. Example, `add r6 r7 r8`: 14 microinstructions (13 + the closing `mloadIR`), 3 redirects, so 18 cycles horizontal and 22 vertical. The 10-instruction program `t_asmblr.txt` takes 101 microinstructions, 128 cycles horizontal, 155 vertical.

---

## 6. Exceptions

A unit raises an exception by setting `excCause`; the micro-sequencer then enters `.exception` instead of the next microinstruction.

| Code | Cause | Raised by |
|---|---|---|
| 1 | illegal opcode (0, 26 to 31, or a malformed instruction word) | switch unit |
| 2 | division / modulo by zero | ALU |
| 3 | data address outside memory | data memory |
| 4, 5 | stack bounds, arithmetic overflow | reserved |

The routine writes **R4 = cause** and **R5 = index of the faulting instruction** (`pc - 1`), then executes `<halt>`:

```
.exception:  mmov regData, excCause
             mmovi regSrc, 4, <write>
             mmov regData, pc
             madd regData, -1
             mmovi regSrc, 5, <write>
             mmov regSrc, regSrc, <halt>
```

An instruction that faults has no effect on registers or memory. Execution does not resume: the ISA has no return-from-exception instruction.

---

## 7. Using the units

```cpp
MicroProgram prog = buildRisc201Microprogram();

HorizontalMicroControlUnit hcu;        // or VerticalMicroControlUnit
hcu.load(prog);                        // micro-assemble and write the control memory (repeatable)
hcu.run(machine);                      // machine.reset(), then run instrMemory to its end

hcu.cycles();  hcu.instructionsExecuted();  hcu.microInstructionsExecuted();
hcu.finishedNormally();  hcu.faulted();  hcu.faultCause();
std::string text = hcu.listing();      // control-store listing; builds text, prints nothing
```

Microcode can be replaced at run time by editing the `MicroProgram` (for example `opcodeLabel[17] = ""` makes `nop` illegal) and calling `load()` again. Nothing is printed by `run()`.

Build: add the four `src/micro*.cpp` files to the compile line. `isa.hpp` is deliberately not included by the new files (it defines non-inline globals in a header).

---

## 8. Limitations and what comes next

- The units are not connected to the pipeline yet. The book's microprogram is a multi-cycle sequence on a shared bus, so how its control words feed an overlapped 4- and 6-stage pipeline is decided together with the pipeline changes.
- `add`, `sub` and `div` overflow is still invisible (only `cmp` updates flags), so arithmetic overflow (cause 5) is never raised.
- Stack-boundary checking (cause 4) is reserved for the stack-protection phase.
- After an exception the machine stops; there is no handler dispatch.
- There is no interactive micro-PC view yet; `listing()` and the counters are the hooks for the CLI.
