# FeMe Roadmap: Finishing the Design, and Growing End-to-End Coverage

## What this document is

[Design.md](Design.md) and [FeMeCPUDesign.md](FeMeCPUDesign.md) each carry
their own "Roadmap / Milestones" section, and each records, inline, which of
its milestones are implemented and how each implementation narrowed the
design (the "Status"/"Deviation" notes). Read together they describe *what
FeMe is*, but neither answers the two questions that matter for planning the
next stretch of work:

1. What is left, across both documents, and in what order should it be done?
2. FeMe now executes shaders (`feme-run`, the CPU target). What should it be
   executing that it isn't, so that the next feature to land is caught by a
   test rather than by a user?

This document answers those two questions. It does not restate either design;
every item below cites the section of Design.md / FeMeCPUDesign.md that owns
the decision, and those documents remain authoritative for *how* a thing
should work. This one is only about *what is missing* and *what order*.

Priorities are relative, not scheduled:

- **P0** — blocks a claim the design already makes, or blocks a milestone
  that is otherwise "done".
- **P1** — needed for the design's stated v1 scope, but nothing currently
  landed depends on it.
- **P2** — genuinely later; listed so it isn't rediscovered as a surprise.

## Part 1: Gap inventory

### 1.1 Core library plumbing

The library-shape parts of Design.md are the least finished part of FeMe,
because every milestone so far has been able to route around them.

| Gap | Owner section | Priority |
|---|---|---|
| `Context` registers no MLIR dialects (`lib/Core/Context.cpp` still carries the `TODO`; every tool registers its own dialects instead) | "`feme::Context`" | P1 |
| No `FormatRegistry`; `Driver` hard-codes `detectFormat` over two formats | "Status: `feme::Driver`" | P1 |
| No `Exporter` interface exists at all — the symmetric half of `Importer` was never written; DXIL/SPIR-V "export" is spelled as a `Backend` today | "`Exporter`" | P1 |
| `Context` has no `setDiagnosticHandler`/`diagnose` at all (`include/feme/Core/Context.h` exposes only the two context accessors); fallible library code returns `llvm::Error` and each tool prints it itself | "Diagnostics and Error Handling" | P1 |
| Thread-safety (one `Context` per thread, stateless components) is a stated invariant with no test asserting it | "Core Architectural Principle" | P0 |
| C API | Design.md milestone 10 | P2 |

The thread-safety item is P0 because it is a *claim in the design*, made
about a library whose primary use case is a multi-threaded driver, that
nothing verifies. A `gtest` that imports/raises/retargets the same input on N
threads with N `Context`s (and, under TSan, fails on any shared mutable
state) is cheap and closes it.

### 1.2 SPIR-V

The SPIR-V *input* half is the narrowest edge of the translation matrix.

- **P0 — `spirv` → `llvm` dialect conversion breadth.** Per "Known gap:
  `spirv` dialect -> `llvm` dialect conversion coverage": sampling ops
  (`OpImageSampleImplicitLod` and friends), `OpImageFetch`/`OpImageGather`,
  `StorageBuffer` blocks (`target("spirv.VulkanBuffer", ...)`), push
  constants, and graphics stage inputs/outputs are all missing. Until these
  land, "SPIR-V input" means "a compute shader whose only resources are
  sampler-less images", which is not enough to run the same HLSL through
  both front ends (see §2.3).
- **P0 — SPIR-V shaders cannot execute.** `feme-run` accepts `.ll`/`.bc`/
  DXIL only (`tools/feme-run/feme-run.cpp`), so the CPU target's entire
  execution-based test suite is DXIL-only. This is the single biggest
  asymmetry in the current test story; see §2.4.
- **P1 — SPIR-V → DXIL direction.** Design.md milestone 6's remaining half:
  a pass raising SPIR-V-derived LLVM IR into DXIL's conventions. Blocked on
  the conversion breadth above.
- **P1 — SPIR-V bound resources.** `SPV_EXT_descriptor_heap` is unraised, so
  `feme::cpu::BoundResourceNormalizationPass` handles DXIL's
  `handlefrombinding` only (FeMeCPUDesign.md milestone 11's deviation note),
  and `handlefromimplicitbinding` has no raiser to produce it.

### 1.3 DXIL

DXIL import is the most complete path, and its gaps are enumerable.

- **P0 — aggregate-returning ops (done by R3).** `IMul`/`UMul`/`UAddc`/
  `SplitDouble`/`WaveActiveBallot` all needed the same general
  multi-return-value `extractvalue`-reconstruction mechanism, which is why
  they were deferred together (Design.md milestone 4 status; FeMeCPUDesign.md
  milestone 1 deviation). `feme::dxil::OpRaisingPass::raiseAggregateCall` is
  that one mechanism, unblocking all five; `WaveActiveBallot` -- the only
  wave intrinsic HLSL programs reach for that FeMe could not lower -- is now
  lowered on the CPU target too (`feme::cpu::WaveCallKind::Ballot` in
  WaveLowering.cpp), exercised end to end by `ballot.hlsl`.
- **P0 — flag-selected opcode families (done by R4).** `WaveActiveOp`/
  `WaveActiveBit`/`WavePrefixOp`/`QuadOp` select their operation from a flag
  operand rather than mapping 1:1 onto an intrinsic; `Barrier` (done
  earlier) likewise. `feme::dxil::OpRaisingPass::raiseReduceOpCall`/
  `raiseWaveActiveBitCall`/`raiseQuadOpCall` raise all four; the CPU target
  lowers every one of them except `QuadOp` (`feme::cpu::WaveLoweringPass::
  lowerActiveReduce`/`lowerPrefixReduce`) -- quad ops need a fixed
  lane-to-quad mapping that remains an explicit v1 non-goal (see
  FeMeCPUDesign.md's "Non-Goals"), so raising `QuadOp` closes the "hard
  pipeline error downstream" risk without yet making it executable.
- **P1 — texture/sampler handle kinds.** Blocked on recovering the
  dimension/multi-sample/feedback bits that binding metadata does not carry
  the way `StructuredBuffer`/`CBuffer`'s size/alignment is; needs a decision
  recorded in Design.md's DXIL section before implementation.
- **P1 — the remaining resource access ops** (non-typed buffer and texture
  load/store beyond the raw/structured forms milestone 10 added).

### 1.4 DXBC / `dxsa`

- **P0 — DXBC is not reachable from `feme` or `Driver` at all.**
  `detectFormat` knows "dxil" and "spirv"; `feme-translate` still carries a
  `TODO` for registering DXBC. The `dxsa` dialect, `BinaryParser` and
  `translateToLLVMIR` all exist and are heavily lit-tested in isolation
  (`test/Target/DXSA`, `test/Translate/DXBC`), but no end-to-end DXBC
  invocation exists — which is Design.md milestone 8 ("DXBC → DXIL end to
  end") in its entirety, and it is mostly *wiring*, not new translation.
- **P0 — DXBC importer fuzzer.** Design.md's Testing Strategy makes "a
  fuzzing harness lands alongside each importer" a v1 requirement, and DXBC
  is the one importer without one (`dxbc-as-fuzzer` fuzzes the assembler's
  text parser, not `BinaryParser`'s binary one). `BinaryParser.cpp` is ~3800
  lines of hand-written token decoding over untrusted input; it is the
  highest-risk unfuzzed surface in the tree.
- **P1 — `BinaryWriter` (`feme::dxsa::serialize`)** is still the
  unimplemented stub inherited from the prototype, and is the hard
  prerequisite for real DXBC *export*.
- **P1 — `translateToLLVMIR` coverage**, in the dependency order its own
  status note gives: resource queries (`bufinfo`/`resinfo`/`sampleinfo`/
  `samplepos`), atomics and UAV counters, groupshared memory, doubles,
  subroutines (`label`/`call`), non-pixel-shader stage declarations.
- **P2 — SM5 opcode coverage** in the dialect/parser itself, incrementally.

### 1.5 Retargeting

- **P1 — NVPTX and AArch64** (Design.md milestone 9's remainder). `Driver`'s
  triple resolution is already generic; what each needs is, at most, a
  counterpart to `feme::amdgpu::RaisedLoweringPass` for the raised
  intrinsics that have no target-independent form.
- **P1 — `feme::amdgpu::RaisedLoweringPass` breadth** tracks §1.2/§1.3: every
  newly-raised intrinsic needs an AMDGPU lowering or it becomes a new
  end-to-end failure on a path that used to work.
- **P2 — MLIR `gpu`-dialect retargeting**, deferred by Design.md's Non-Goals
  until a client needs it. No action.

### 1.6 CPU target

FeMeCPUDesign.md milestones 1–11 are done; 12 (resource performance) and 13
(general performance) are explicitly correctness-first-then-measure and stay
P2. What is P0/P1 is the set of things earlier milestones *narrowed*, each of
which is a diagnostic today and a wrong answer tomorrow if forgotten:

| Narrowing | Milestone | Priority |
|---|---|---|
| Root constants unimplemented; a bound constant buffer is an unsupported resource kind | 3/11 | P1 |
| Barrier inside a surviving branch or loop is diagnosed, not split | 9 | P1 |
| No SSA value may be live across a group-sync barrier | 9 | P1 |
| Divergent groupshared access is diagnosed (including a groupshared `atomicrmw`/`load`/`store` reached through a `getelementptr`, even one every index of which is constant -- only a *direct* global reference, with no intervening `getelementptr` at all, is canonicalized as of R2; see `feme/test/Transforms/CPU/simdize-groupshared-atomic-scalar.ll`'s comment) | 9 | P1 |
| Vector/aggregate leaf decomposition unimplemented | 7 | P1 |
| Masked memory always lowers to `gather`/`scatter` (no uniform/contiguous cases) | 7 | P2 (perf) |
| `WaveReadLaneAt`'s lane operand assumed uniform | 8 | P1 |
| `Device` vs `All` memory scope not distinguished | 9 | P2 |
| Dispatch is sequential, not thread-pooled | 4 | P1 |
| `feme-run`'s heap YAML has no `class`/`kind`/`stride`/`format` | 11 | P0 (see §2.4) |

R2 closed both of this table's former P0 rows: the scalarization fallback
now masks a divergent `atomicrmw`'s per-lane execution (an unmasked lane in
a scalarized atomic was not a crash, it was a silently wrong answer, which
is why it was P0 -- see `feme::cpu::FunctionWidener::widenMaskedAtomicRMW`,
`feme/test/Tools/feme-run/HLSL/histogram.hlsl`), and `feme::cpu::runPipeline`
now fails outright, with the underlying diagnostic surfaced, the moment any
CPU-pipeline pass (`feme::cpu::LinearizePass`/`SIMDizePass` in particular)
reports one instead of silently continuing to a later pass or the JIT (see
`feme::cpu::ErrorDiagnosticGuard` in Pipeline.cpp) -- investigating the
"`feme-cpu-simdize` divergent-branch check does not catch every such case"
symptom found that check itself (`FunctionWidener::checkSupportedControlFlow`)
already correctly flags every shape `feme-cfg-gen --unstructured` produces
that `feme::cpu::LinearizePass` leaves untouched (verified directly via
`feme-opt -passes=feme-cpu-linearize,feme-cpu-simdize`); the actual gap was
that neither pass's diagnostic ever stopped `runPipeline` from linking and
JIT-dispatching the untouched, still-divergent function anyway.

### 1.7 Robustness

- **P0 — the fuzzers do not run anywhere.** Three libFuzzer targets exist
  (`feme-dxil-import-fuzzer`, `feme-spirv-import-fuzzer`, `dxbc-as-fuzzer`)
  plus `feme-cpu-restructure-fuzzer`; Design.md says they are "run in CI
  alongside the `lit`/`gtest` suites". Nothing runs them — none of the four
  is even in `FEME_TEST_DEPENDS` (`test/CMakeLists.txt`), so `check-feme`
  does not build them and a fuzzer that stops compiling would go unnoticed.
  A bounded (`-runs=N`, seed-corpus-only) `check-feme-fuzz` target that each
  fuzzer registers with would make that claim true for a few seconds of test
  time; adding them to `FEME_TEST_DEPENDS` closes the build half on its own.
- **P1 — sanitizer coverage.** The one previously-recorded crash class in
  this tree (see `agent_thoughts.md`) was a UB/null-deref bug found by hand;
  an ASan/UBSan `check-feme` configuration is the systematic version.

## Part 2: End-to-end testing roadmap

### 2.1 What exists today

| Layer | Tests |
|---|---|
| CLI translation | `test/Tools/feme/feme-dxil-to-{dxil,spirv,amdgpu}.ll`, `feme-spirv-{null-pipeline,compute-shader,to-amdgpu}.mlir` |
| CLI CPU target | `test/Tools/feme/feme-cpu-{loop,wave-size,accept-bound-resource,reject-unbounded-register-bound}.ll` |
| Execution, hand-written IR | `test/Tools/feme-run/{thread-id-store,multi-group-dispatch,dxil-container-input,reference-mode}.ll` |
| Execution, real HLSL | `test/Tools/feme-run/HLSL/{loop,divergent-control-flow,wave-ops,barrier-groupshared,combined}.hlsl` |
| Differential | `test/Tools/feme-run/differential-harness.test` (5 seeds) |
| Execution, C++ | `unittests/Target/CPU/{JITEngineTest,AOTDispatchTest}.cpp` |

That is real end-to-end coverage — HLSL source through Clang, DXIL import,
raising, the whole CPU pipeline, JIT dispatch, and a `FileCheck`ed numeric
result. The gaps below are about *breadth of axis*, not about the pipeline
being untested.

### 2.2 The axes not yet covered

1. **Wave size** (done by R1). Three of the five executing HLSL end-to-end
   tests whose own computation does not depend on wave width (`loop.hlsl`,
   `divergent-control-flow.hlsl`, `barrier-groupshared.hlsl`) now run at `W`
   in {4, 8, 16, 32} via the `%feme-wave-size-sweep` lit substitution
   (§2.4.1); `wave-ops.hlsl`/`combined.hlsl` genuinely compute a
   wave-size-dependent answer (they use a wave intrinsic directly) and stay
   at `W = 4` until a per-wave-size expected value is worth adding.
2. **The differential harness's own scope** (done for `--divergent`/
   `--loops` by R1; `--unstructured` remains `--reference`-only). Its header
   comment used to say its `--divergent=false --loops=false
   --unstructured=false` restriction existed because milestone 4's widener
   was acyclic/uniform-only, "at which point this harness's scope should
   grow with them". Milestones 6 and 7 landed; the harness did not grow
   until R1, which turns `--divergent`/`--loops` on (the configuration that
   exercises the linearizer and the widened-loop path against ground
   truth) and found the new P0 gap in §1.6's table when `--unstructured`
   was also tried against the normal pipeline (a divergent branch inside a
   loop that should have been diagnosed instead reaching the JIT
   unwidened). `--unstructured` stays `--reference`-only (still real
   coverage: it also caught, and this milestone fixed, a genuine
   `feme-cfg-gen` termination bug in its own irreducible-edge construct)
   until that gap closes.
3. **Front-end equivalence.** No test compiles one HLSL source to *both*
   DXIL and SPIR-V and asserts the two produce the same answer. This is the
   test that would give the SPIR-V input path the same confidence the DXIL
   one has, and it is blocked only on §1.2's two P0 items.
4. **JIT vs AOT.** `AOTDispatchTest.cpp` establishes the pattern in `gtest`
   for one shader; no `lit` test compiles a shader with `feme --target=<host>`
   and executes the resulting object file. AOT is what an embedding client
   actually ships.
5. **Optimization level.** Every end-to-end test runs at the default `-O0`.
   `-O2` reorders and vectorizes the raised IR before the CPU pipeline sees
   it; nothing checks that a shader still computes the same answer.
6. **Round trips.** `feme-dxil-to-dxil.ll` checks a DXIL→DXIL retarget
   produces a container, but no test re-imports the produced artifact and
   executes it (DXIL→DXIL→run), nor re-imports produced SPIR-V. A round trip
   that *executes* is a much stronger statement than one that parses.
7. **Resource shapes.** Every executing test uses an unstructured raw buffer,
   because that is all `feme-run`'s heap YAML can describe (§1.6). Typed
   buffers, formats, strides and constant buffers are untested by execution.
8. **DXBC.** Zero end-to-end coverage, per §1.4.

### 2.3 The interesting cases to add

Concretely, in rough value order, all under `test/Tools/feme-run/HLSL/`
unless noted:

- **`reduction.hlsl`** — a groupshared tree reduction with barriers inside a
  loop. Directly exercises §1.6's "barrier inside a loop is diagnosed"
  narrowing, and is the single most common real compute-shader shape not yet
  covered.
- **`prefix-sum.hlsl`** (done by R4) — `WavePrefixSum`/`WavePrefixCountBits`
  over a divergent mask; exercises §1.3's flag-selected `WavePrefixOp`
  family.
- **`histogram.hlsl`** — divergent atomics into a shared buffer. This is the
  scalarization fallback's only realistic workload, and the one that catches
  §1.6's unmasked-lane P0 (done: a single groupshared counter a divergent
  condition gates `InterlockedAdd` into, reading the atomic's own return
  value rather than a separate reload -- a genuine multi-bucket histogram,
  indexing a groupshared array by a divergent bucket, remains blocked on
  §1.6's separate "Divergent groupshared access is diagnosed" row: Clang
  itself folds an `if`/`else` each doing the same op on a different constant
  address into a single `select`-of-pointer `atomicrmw`, the address-
  divergent shape that narrowing already covers).
- **`ballot.hlsl`** (done by R3) — `WaveActiveBallot` + `countbits`; was
  gated on §1.3's aggregate-returning mechanism.
- **`nested-divergence.hlsl`** — divergent loop containing a divergent
  diamond containing an early return. The linearizer's hardest supported
  shape, currently only reached through generated CFGs.
- **`multi-group-barrier.hlsl`** — several groups, each with barriers and
  groupshared state, asserting groups do not observe each other's memory.
- **`matrix-multiply.hlsl`** — tiled, groupshared-staged, loop-heavy; the
  closest thing to a real workload, and a natural first performance
  regression fixture for milestone 12/13.
- **`typed-buffer.hlsl`** — `RWBuffer<float4>` with a real format, gated on
  §2.4's heap YAML work.
- **A DXBC pair** under `test/Tools/feme/`: a `dxbc-as`-assembled `.dxasm`
  fixture retargeted with `feme --target=dxil`, and the same fixture executed
  through `feme-run` once DXBC import reaches `Driver` (§1.4).

Each of these is written the way the existing HLSL tests are — `split-file`,
`clang -target dxil--shadermodel6.5-compute`, a `heap.yaml`, a `CHECK` line
of numbers — so the incremental cost per shader is small once the
prerequisites in §2.4 exist.

### 2.4 Test infrastructure prerequisites

These are the changes that make §2.3 cheap rather than repetitive, and each
is small enough to land on its own:

1. **A wave-size sweep substitution** (done by R1: `%feme-wave-size-sweep`,
   feme/utils/feme-wave-size-sweep.py). A lit substitution that runs one
   input at several wave sizes and `FileCheck`s each run's output, so a
   shader opts into the sweep by one substitution instead of four `RUN:`
   lines.
2. **`feme-run` SPIR-V input.** `feme-run` links `FeMeImportDXIL` only; the
   work is linking the SPIR-V importer and its `Translator` chain and
   reusing `Driver`'s existing format detection rather than re-deriving it,
   and it is what unlocks §2.2's front-end-equivalence axis.
3. **Heap YAML `kind`/`format`/`stride`.** FeMeCPUDesign.md already specifies
   the richer schema; milestone 11 shipped the raw-buffer subset. Filling it
   in unlocks §2.3's typed-buffer cases and stops tests from hand-encoding
   float bit patterns as integers.
4. **A `%feme-run-differential` harness helper** (done by R1:
   feme/utils/feme-run-differential.py). Takes a seed list, a `feme-cfg-gen`
   flag set, and a wave-size list, generating each seed once and diffing its
   `--reference` output against every requested wave size -- replacing the
   differential harness's previous five copy-pasted five-line blocks with
   one substitution per shape/wave-size combination, so §2.2's item 2 can
   grow the shape space without growing the file. It also refuses to trust
   a `feme-run` invocation that printed a diagnostic even if its exit code
   was zero (see §1.6's new divergent-branch-in-a-loop gap, found by this
   helper), so a shape is only accepted once the pipeline ran it with no
   caveats.
5. **An AOT lit recipe.** `feme --target=<host-triple>` + a tiny loader
   (either a `feme-run --object` mode or a test-only C driver) so AOT is
   covered by `lit`, not only by `gtest`.
6. **`check-feme-fuzz`.** Per §1.7: bounded runs over the checked-in seed
   corpora, wired as a dependency of nothing by default but runnable in CI.

### 2.5 Corpus discipline

Two rules the existing tests already follow, worth stating so the additions
keep following them:

- **No binary fixtures.** Every input is assembled at test time — `.hlsl`
  through `clang`, `.ll` through `llvm-as`/`llc`, `.yaml` through
  `yaml2obj`, `.dxasm` through `dxbc-as` (Design.md, "Avoiding binary test
  fixtures").
- **Every restructurization bug found anywhere reduces into a named shape
  file** under `test/Transforms/CPU/CFG/` (FeMeCPUDesign.md, "CFG
  restructurization test suite", layer 1).

## Part 3: Suggested sequencing

Each step is independently landable and independently testable; the
dependency column is the only ordering constraint.

| # | Step | Covers | Depends on |
|---|---|---|---|
| R1 | Grow the differential harness to divergent/loop shapes; add the wave-size sweep (done: `--unstructured` stays `--reference`-only, see §1.6's new gap) | §2.2.1, §2.2.2, §2.4.1, §2.4.4 | — |
| R2 | Mask the scalarization fallback's per-lane execution; add `histogram.hlsl`; make `feme-cpu-simdize` reject every shape `feme-cpu-linearize` left an unwidened divergent branch in, including one inside a loop (§1.6's new gap, found by R1) (done: the divergent-branch gap turned out to be `feme::cpu::runPipeline` not propagating a pass diagnostic, not `feme-cpu-simdize`'s own check -- see §1.6's table) | §1.6 P0, §2.3 | R1 (harness catches regressions) |
| R3 | Multi-return-value raising mechanism (`IMul`/`UMul`/`UAddc`/`SplitDouble`/`WaveActiveBallot`) + `ballot.hlsl` (done: `feme::dxil::OpRaisingPass::raiseAggregateCall` raises all five; `feme::cpu::WaveCallKind::Ballot`/`lowerBallot` lower `WaveActiveBallot` on the CPU target) | §1.3 P0 | — |
| R4 | Flag-selected opcode families (`WaveActiveOp`/`WaveActiveBit`/`WavePrefixOp`/`QuadOp`/`Barrier`) + `prefix-sum.hlsl` (done: `feme::dxil::OpRaisingPass` raises all four remaining families; `feme::cpu::WaveLoweringPass` lowers every one of them except `QuadOp`, which stays raised-only pending the quad/derivative lane mapping FeMeCPUDesign.md's "Non-Goals" defers) | §1.3 P0 | — |
| R5 | Barriers inside branches/loops; values live across barriers; `reduction.hlsl`, `multi-group-barrier.hlsl` | §1.6, §2.3 | R4 (`Barrier` raising) |
| R6 | DXBC importer fuzzer; `check-feme-fuzz` | §1.4 P0, §1.7 P0 | — |
| R7 | DXBC through `Driver`/`feme`/`feme-translate` — Design.md milestone 8 end to end | §1.4 P0, §2.2.8 | — |
| R8 | Heap YAML `kind`/`format`/`stride`; `typed-buffer.hlsl`; AOT lit recipe | §2.4.3, §2.4.5, §2.2.4 | — |
| R9 | `spirv`→`llvm` dialect breadth (storage buffers, sampling, push constants) | §1.2 P0 | — |
| R10 | `feme-run` SPIR-V input; one HLSL source executed through both front ends | §1.2 P0, §2.2.3 | R9 |
| R11 | Thread-safety test; route library diagnostics through `Context`; `FormatRegistry`; `Exporter` interface | §1.1 | R7 (a third format makes the registry pay) |
| R12 | Root constants; `WaveReadLaneAt` with a varying lane; vector/aggregate decomposition | §1.6 P1 | — |
| R13 | SPIR-V → DXIL direction; `BinaryWriter`; NVPTX/AArch64 | §1.2, §1.4, §1.5 P1 | R9 |
| R14 | `-O2` end-to-end differential; execute-after-round-trip tests | §2.2.5, §2.2.6 | R8 |
| R15 | CPU milestones 12/13 (resource and general performance), C API | §1.5, §1.6 P2, Design.md milestone 10 | R1–R14 |

R1 comes first deliberately, and for the same reason FeMeCPUDesign.md put its
own restructurization suite before its linearizer: it is the step that makes
every subsequent step's failures visible as a diff instead of as a wrong
number in a `CHECK` line nobody wrote yet.

## Part 4: Explicitly not scheduled

- MLIR `gpu`-dialect retargeting (Design.md Non-Goals — no client).
- DXIL → DXBC and SPIR-V → DXBC (Translation Matrix: "not a priority").
- HLSL/GLSL source front ends (Design.md Non-Goals).
- Standalone out-of-tree builds against an installed LLVM+MLIR (Design.md
  Goals: out of scope for now, no redesign needed to add later).
