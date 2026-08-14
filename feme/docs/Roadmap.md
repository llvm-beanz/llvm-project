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

- **P0 — `spirv` → `llvm` dialect conversion breadth (done by R9).** Per
  "Known gap: `spirv` dialect -> `llvm` dialect conversion coverage":
  `StorageBuffer` blocks (`RWStructuredBuffer<T>`/`StructuredBuffer<T>`,
  converting to `target("spirv.VulkanBuffer", ...)`), push constants, basic
  (unmodified) `spirv.ImageSampleImplicitLod` sampling and `OpImageFetch`
  (which reuses `spirv.ImageRead`'s exact lowering -- LLVM's SPIRV backend
  itself picks the opcode from the handle's image type) all now convert.
  Still missing, and narrower than originally scoped here: the sampling
  bias/gradient/explicit-LOD/comparison/gather variants (each needs its own
  pattern supplying additional operands), `Uniform`-storage-class buffer
  blocks (`cbuffer`/`ConstantBuffer<T>`, which real `clang`-compiled access
  spells entirely differently -- per-member globals in a separate address
  space tied together by `!hlsl.cbs` metadata -- and so needs its own design
  decision rather than reusing the storage buffer access chain pattern), and
  graphics stage inputs/outputs. "SPIR-V input" now covers a compute shader
  binding, reading and writing storage buffers and sampling a texture, which
  is enough to run more (but not yet all) HLSL through both front ends (see
  §2.3).
- **P0 — SPIR-V shaders cannot execute (done by R10).** `feme-run` now
  links `FeMeImportSPIRV`/`FeMeTranslateSPIRV` too (see §2.4.2), so a
  SPIR-V binary module is imported and translated to LLVM IR the same way
  a DXIL bitcode file/DXContainer is; `feme::cpu::SPIRVResourceLoweringPass`
  and `feme::cpu::SPIRVBuiltinFoldingPass` (new CPU-pipeline passes) let a
  storage-buffer compute shader using thread/group ID builtins execute,
  closing this asymmetry for that shape of shader (still not every
  resource kind §1.2's conversion-breadth note above lists as open --
  image/sampler resources and per-field structured-buffer access remain
  future work on the CPU-execution side specifically). See §2.4.2 and the
  Deviation note roadmap step R10 adds to feme/docs/FeMeCPUDesign.md's
  Status section.
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

- **P0 — DXBC is not reachable from `feme` or `Driver` at all (done by
  R7).** `detectFormat` now distinguishes a legacy DXBC `DXContainer` (an
  `SHEX`/`SHDR` part) from a DXIL one (a `DXIL`/`ILDB` part) sharing the
  same "DXBC" magic; `feme::DXBCImporter` (`feme/lib/Import/DXBC`) parses
  the former into the `dxsa` dialect via the existing `BinaryParser`, and
  `feme::dxsa::DXSAToLLVMIRTranslator` wraps the existing
  `translateToLLVMIR` behind the `Translator` interface so `feme::Driver`
  dispatches to it exactly like `SPIRVToLLVMTranslator`. Both are
  registered with `feme-translate` (`--import-dxbc`), closing Design.md
  milestone 8 ("DXBC → DXIL end to end") -- see
  `test/Tools/feme/feme-dxbc-to-dxil.dxasm` for the resulting end-to-end
  DXBC-in, DXIL-`DXContainer`-out round trip through the full `feme` CLI.
  This was indeed mostly wiring, but exposed one real bug in
  `translateToLLVMIR` itself along the way: a UAV's `!dx.resources` entry
  was missing the three `i1` flags (globally-coherent/has-counter/
  rasterizer-ordered) a UAV entry needs beyond an SRV's, which silently
  made every DXBC-derived UAV unraisable (`feme::dxil::OpRaisingPass`
  could never look its binding up) -- invisible until an actual retargeting
  path existed to notice, since no existing test `FileCheck`ed that far
  into the metadata. Retargeting a DXBC-derived module inherits every
  limitation retargeting a DXIL one already has (see §1.3), plus
  `translateToLLVMIR`'s own remaining gaps below -- notably, no DXBC
  graphics-stage shader is retargetable yet at all (`loadInput`/
  `storeOutput` are not raised, a gap shared with real DXIL input, see
  `feme/docs/CommandGuide/feme.md`), and a DXBC compute shader's declared
  `NumThreads` does not yet reach DXIL's metadata.
- **P0 — DXBC importer fuzzer (done by R6).** Design.md's Testing Strategy
  makes "a fuzzing harness lands alongside each importer" a v1 requirement,
  and DXBC was the one importer without one (`dxbc-as-fuzzer` fuzzes the
  assembler's text parser, not `BinaryParser`'s binary one).
  `feme-dxbc-import-fuzzer` (`feme/tools/feme-dxbc-import-fuzzer`) now
  fuzzes `feme::dxsa::deserialize` directly, and immediately found a real
  crash: a malformed `l`/`d` immediate source operand whose decoded
  component count didn't match its payload hit an unchecked
  `SrcOperandAttr::get` builder call and asserted instead of surfacing a
  diagnostic; `BinaryParser.cpp` now builds it with `getChecked`, falling
  back to the existing `dxsa.unknown` diagnostic path like every other
  malformed-operand case (see
  `test/Target/DXSA/src_operand_immediate_zero_components_invalid.dxasm`).
- **P1 — `BinaryWriter` (`feme::dxsa::serialize`)** is still the
  unimplemented stub inherited from the prototype, and is the hard
  prerequisite for real DXBC *export*.
- **P1 — `translateToLLVMIR` coverage**, in the dependency order its own
  status note gives: resource queries (`bufinfo`/`resinfo`/`sampleinfo`/
  `samplepos`), atomics and UAV counters, groupshared memory, doubles,
  subroutines (`label`/`call`), non-pixel-shader stage declarations (which
  includes the `NumThreads` gap R7 found, above).
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
| Barrier inside a surviving *branch* (not a loop; R5 closed the loop case) is diagnosed, not split | 9 | P1 |
| Divergent groupshared access is diagnosed (including a groupshared `atomicrmw`/`load`/`store` reached through a `getelementptr`, even one every index of which is constant, and a *masked* store even at a uniform address -- found by R5's `reduction.hlsl` -- only a *direct*, unconditionally-executed global reference, with no intervening `getelementptr` at all, is canonicalized as of R2; see `feme/test/Transforms/CPU/simdize-groupshared-atomic-scalar.ll`'s comment) | 9 | P1 |
| Vector/aggregate leaf decomposition unimplemented | 7 | P1 |
| Masked memory always lowers to `gather`/`scatter` (no uniform/contiguous cases) | 7 | P2 (perf) |
| `WaveReadLaneAt`'s lane operand assumed uniform | 8 | P1 |
| `Device` vs `All` memory scope not distinguished | 9 | P2 |
| Dispatch is sequential, not thread-pooled | 4 | P1 |
| `feme-run`'s heap YAML has no `class`/`kind`/`stride`/`format` | 11 | P0 (see §2.4) |

R5 closed this table's "no SSA value may be live across a group-sync
barrier" row outright (`feme::cpu::spillValuesLiveAcrossBarriers`) and
narrowed the "barrier inside a surviving branch or loop" row to just
branches: `feme::cpu::matchLoopShape`/`buildWrapperForLoop` split a barrier
inside a uniform, header-tested loop (the stride-halving reduction shape
`reduction.hlsl` compiles to) by cloning the loop's header/latch directly
into the wrapper as an ordinary scalar loop and running the barrier-split
body regions through the usual per-wave wave loop once per iteration. It
also found (while writing `reduction.hlsl`) that the groupshared-access
narrowing was one case broader than recorded: not just a divergent
*index*, but also a *masked* (conditionally-executed) store at a uniform
address, since `feme::cpu::LinearizePass` lowers that into a call
`feme::cpu::rewriteGroupSharedGlobals` does not recognize -- both remain
open.

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

- **P0 — the fuzzers do not run anywhere (done by R6).** Five libFuzzer
  targets now exist (`feme-dxil-import-fuzzer`, `feme-dxbc-import-fuzzer`,
  `feme-spirv-import-fuzzer`, `dxbc-as-fuzzer`,
  `feme-cpu-restructure-fuzzer`); all five are in `FEME_TEST_DEPENDS`
  (`test/CMakeLists.txt`), so `ninja check-feme` builds every one, and a
  new `check-feme-fuzz` CMake target (driven by
  `feme/utils/check-feme-fuzz.py`) runs each fuzzer for a bounded number of
  iterations (`-runs=N`/`-max_total_time=N`) over its own checked-in seed
  corpus. Wiring the build half up alone was enough to catch a real,
  already-landed regression: `dxbc-as-fuzzer.cpp` had bit-rotted against a
  `wrapInContainer` signature change (a `Signatures` parameter added by a
  later commit) and no longer compiled, unnoticed because nothing built it
  — exactly the failure mode this item warned about.
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
3. **Front-end equivalence** (done by R10). Both of §1.2's P0 items now
   close: `test/Tools/feme-run/HLSL/front-end-equivalence.hlsl` runs a
   `RWStructuredBuffer<float>` compute shader through both the DXIL and
   SPIR-V front ends and `FileCheck`s the same expected numbers from each.
   The DXIL half is real HLSL compiled by Clang; the SPIR-V half is
   hand-written `spirv` dialect MLIR (see that test's own comment for why
   -- Clang's HLSL front end only reaches SPIR-V through LLVM's in-tree
   SPIRV backend, which this roadmap step's own build did not configure),
   so it is not literally *one* `.hlsl` source compiled twice, but the same
   shader's logic independently authored for each front end and checked
   for agreement, which is the property this item exists to give SPIR-V
   input the same confidence the DXIL side already has.
4. **JIT vs AOT** (done by R8). `AOTDispatchTest.cpp` establishes the pattern
   in `gtest` for one shader; `feme-run --object` now loads a shader
   compiled with `feme --target=<host>` -- a real object file -- with
   `orc::LLJIT::addObjectFile` and dispatches it directly, so `lit` covers
   the same AOT path (see test/Tools/feme-run/feme-run-object-aot.ll).
5. **Optimization level.** Every end-to-end test runs at the default `-O0`.
   `-O2` reorders and vectorizes the raised IR before the CPU pipeline sees
   it; nothing checks that a shader still computes the same answer.
6. **Round trips.** `feme-dxil-to-dxil.ll` checks a DXIL→DXIL retarget
   produces a container, but no test re-imports the produced artifact and
   executes it (DXIL→DXIL→run), nor re-imports produced SPIR-V. A round trip
   that *executes* is a much stronger statement than one that parses.
7. **Resource shapes** (typed buffers done by R8). Every executing test
   used to use an unstructured raw buffer, because that was all
   `feme-run`'s heap YAML could describe (§1.6); it now also accepts
   `kind`/`format`/`stride`, and `typed-buffer.hlsl` exercises a real
   `RWBuffer<float4>`. Structured-buffer strides and constant buffers
   remain untested by execution (the heap YAML can describe them, but no
   test yet does).
8. **DXBC.** CLI-level (`feme`/`feme-translate`) end-to-end coverage now
   exists, per §1.4's R7 entry (`test/Tools/feme/feme-dxbc-to-dxil.dxasm`).
   Execution (a DXBC-derived module reaching `feme-run`/the CPU target) is
   still uncovered -- `feme-run` only accepts `.ll`/`.bc`/DXIL, per §1.2.

### 2.3 The interesting cases to add

Concretely, in rough value order, all under `test/Tools/feme-run/HLSL/`
unless noted:

- **`reduction.hlsl`** (done by R5) — a barrier inside a loop, folding
  per-lane contributions with `WaveActiveSum` and publishing the result
  through groupshared. Directly exercises §1.6's "barrier inside a loop is
  diagnosed" narrowing (now closed for this uniform-loop shape); an honest
  groupshared *tree* reduction (indexing `groupshared` by
  `SV_GroupThreadID`) remains blocked on §1.6's separate "Divergent
  groupshared access is diagnosed" row, which R5 found is one case broader
  than recorded (a *masked* store at a uniform address, not just a
  divergent index).
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
- **`multi-group-barrier.hlsl`** (done by R5) — several groups, each
  publishing through two barrier-separated groupshared slots, asserting
  groups do not observe each other's memory.
- **`matrix-multiply.hlsl`** — tiled, groupshared-staged, loop-heavy; the
  closest thing to a real workload, and a natural first performance
  regression fixture for milestone 12/13.
- **`typed-buffer.hlsl`** (done by R8) — `RWBuffer<float4>` with a real
  format (`r32g32b32a32_float`), using §2.4's heap YAML work.
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
2. **`feme-run` SPIR-V input** (done by R10). `feme-run` now links
   `FeMeImportSPIRV`/`FeMeTranslateSPIRV` alongside `FeMeImportDXIL`, and
   `loadModule` sniffs a SPIR-V binary's own magic number the same way
   `feme::Driver::detectFormat` does, rather than re-deriving that logic
   (the DXIL/SPIR-V import + translation split is small enough that
   sharing `Driver`'s exact detection code was not worth the coupling; see
   `feme-run`'s own file comment). Unlocks §2.2's front-end-equivalence
   axis, once the CPU pipeline itself could also execute a SPIR-V-sourced
   module's resource access and builtin-variable idiom (see
   `feme::cpu::SPIRVResourceLoweringPass`/`SPIRVBuiltinFoldingPass` and
   the Deviation note roadmap step R10 adds to
   feme/docs/FeMeCPUDesign.md's Status section).
3. **Heap YAML `kind`/`format`/`stride`** (done by R8). FeMeCPUDesign.md
   already specified the richer schema; milestone 11 shipped the raw-buffer
   subset. `feme-run`'s `resource-heap`/`bindings` entries now accept all
   three, unlocking §2.3's typed-buffer cases and stopping tests from
   hand-encoding float bit patterns as integers.
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
5. **An AOT lit recipe** (done by R8). `feme-run --object` loads a shader
   compiled with `feme --target=<host-triple>` -- a real object file -- with
   `orc::LLJIT::addObjectFile` and dispatches it directly through
   `feme::cpu::runDispatch`, so AOT is covered by `lit`, not only by
   `gtest` (test/Tools/feme-run/feme-run-object-aot.ll).
6. **`check-feme-fuzz` (done by R6).** Per §1.7: bounded (`-runs=N`,
   seed-corpus-only) runs of every fuzz target, wired as a dependency of
   nothing by default (`ninja check-feme-fuzz` runs it explicitly) but with
   every fuzz target added to `FEME_TEST_DEPENDS` so `ninja check-feme`
   still builds all of them.

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
| R5 | Barriers inside a uniform loop; values live across barriers; `reduction.hlsl`, `multi-group-barrier.hlsl` (done: `feme::cpu::matchLoopShape`/`buildWrapperForLoop` split a barrier inside a header-tested uniform loop by cloning its header/latch into the wrapper as an ordinary scalar loop; `feme::cpu::spillValuesLiveAcrossBarriers` spills any value live across a barrier into a per-wave context array; a barrier inside a *branch* remains diagnosed, and a divergent groupshared access -- including a masked store at a uniform address, found writing `reduction.hlsl` -- remains a separate, still-open narrowing, see §1.6) | §1.6, §2.3 | R4 (`Barrier` raising) |
| R6 | DXBC importer fuzzer; `check-feme-fuzz` (done: `feme-dxbc-import-fuzzer` fuzzes `feme::dxsa::deserialize` directly and found/fixed a real `SrcOperandAttr` builder assertion on a malformed immediate operand; `check-feme-fuzz` runs all five fuzz targets, seed-corpus-only, and found/fixed a bit-rotted `dxbc-as-fuzzer` call site broken by an unrelated `wrapInContainer` signature change) | §1.4 P0, §1.7 P0 | — |
| R7 | DXBC through `Driver`/`feme`/`feme-translate` — Design.md milestone 8 end to end (done: `feme::DXBCImporter` + `feme::dxsa::DXSAToLLVMIRTranslator` wired into `feme::Driver`/`feme`/`feme-translate --import-dxbc`; found/fixed a latent `!dx.resources` UAV-metadata bug the new end-to-end path exposed, see §1.4) | §1.4 P0, §2.2.8 | — |
| R8 | Heap YAML `kind`/`format`/`stride`; `typed-buffer.hlsl`; AOT lit recipe (done: `feme-run`'s heap YAML `resource-heap`/`bindings` entries accept `kind`/`format`/`stride`, closing the "raw buffers only" narrowing §2.2's "Resource shapes" row and milestone 11's own deviation note flagged; `typed-buffer.hlsl` gives `femeCpuResourceLoadTypedV4F32`/`StoreTypedV4F32` real execution coverage; `feme-run --object` loads a real `feme --target=<host>`-compiled object file with `orc::LLJIT::addObjectFile` and dispatches it through the `feme::cpu::runDispatch` loop factored out of `JITEngine::dispatch` for this reuse, closing §2.2's "JIT vs AOT" row for `lit` -- it has no `ResourceInfo` to place a `bindings` entry's reserved prefix, so only `resource-heap` is supported in that mode) | §2.4.3, §2.4.5, §2.2.4 | — |
| R9 | `spirv`→`llvm` dialect breadth (storage buffers, sampling, push constants) (done: `StorageBuffer` blocks convert to `target("spirv.VulkanBuffer", ...)` handles with `llvm.spv.resource.getpointer`/GEP access chains; `PushConstant` variables convert to an ordinary global in address space 13, which LLVM's own `SPIRVPushConstantAccess` pass rewrites the rest of the way; basic `spirv.ImageSampleImplicitLod` sampling and `OpImageFetch` (reusing `spirv.ImageRead`'s lowering) convert -- sampling variants needing extra operands, `Uniform`-storage-class `cbuffer`/`ConstantBuffer<T>` blocks (a differently-shaped problem, see §1.2's updated note) and graphics stage inputs/outputs remain open) | §1.2 P0 | — |
| R10 | `feme-run` SPIR-V input; one HLSL source executed through both front ends (done: `feme-run` links `FeMeImportSPIRV`/`FeMeTranslateSPIRV`, sniffing SPIR-V's own magic number the same way `feme::Driver::detectFormat` does; `feme::cpu::SPIRVResourceLoweringPass` normalizes a bound `spirv.VulkanBuffer` storage-buffer handle directly into the same canonical `feme.cpu.resource.*` calls the DXIL `BoundResourceNormalizationPass`/`ResourceLoweringPass` pair produces (SPIR-V has no bindless heap to normalize into, so one pass suffices where DXIL needs two), and `feme::cpu::SPIRVBuiltinFoldingPass` folds the `insertelement`-chain-then-`extractelement` idiom SPIR-V's builtin-variable materialization always produces back into the single scalar lane read, matching DXIL's already-scalar `llvm.dx.thread.id` -- `test/Tools/feme-run/HLSL/front-end-equivalence.hlsl` runs a `RWStructuredBuffer<float>` shader through both front ends and checks the same expected numbers from each, though the SPIR-V half is hand-written `spirv` dialect MLIR rather than compiled by Clang from the same `.hlsl` file, since this build configures no LLVM SPIRV backend for Clang's own HLSL-to-SPIR-V path to use; image/sampler resources and per-field structured-buffer access remain unexecutable on the CPU target) | §1.2 P0, §2.2.3 | R9 |
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
