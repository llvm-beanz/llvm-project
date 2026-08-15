# FeMe Roadmap: Finishing the Design, and Growing End-to-End Coverage

## What this document is

[Design.md](Design.md), [FeMeCPUDesign.md](FeMeCPUDesign.md),
[FeMeGraphicsDesign.md](FeMeGraphicsDesign.md),
[FeMeVulkanDesign.md](FeMeVulkanDesign.md) and
[FeMeWARPDesign.md](FeMeWARPDesign.md) each carry their own "Roadmap /
Milestones" section, and each records, inline, which of its milestones are
implemented and how each implementation narrowed the design (the
"Status"/"Deviation" notes). Read together they describe *what FeMe is*, but
none answers the two questions that matter for planning the next stretch of
work:

1. What is left, across both documents, and in what order should it be done?
2. FeMe now executes shaders (`feme-run`, the CPU target). What should it be
   executing that it isn't, so that the next feature to land is caught by a
   test rather than by a user?

This document answers those two questions. It does not restate any design;
every item below cites the section of the design document that owns the
decision, and those documents remain authoritative for *how* a thing should
work. This one is only about *what is missing* and *what order*.

Priorities are relative, not scheduled:

- **P0** — blocks a claim the design already makes, or blocks a milestone
  that is otherwise "done".
- **P1** — needed for the design's stated v1 scope, but nothing currently
  landed depends on it.
- **P2** — genuinely later; listed so it isn't rediscovered as a surprise.

### The two tracks

Everything through §1.7 and R1–R15 is the *retargeting and compute
execution* track: FeMe as a library that imports DXIL/SPIR-V/DXBC, retargets
it, and executes compute shaders on the CPU. That track is the one every
landed milestone belongs to.

§1.8–§1.10 and R16 onward are the *graphics and API runtime* track added by
FeMeGraphicsDesign.md, FeMeVulkanDesign.md and FeMeWARPDesign.md. Nothing in
it exists in tree today — there is no `feme::ShaderStage`, no signature
reflection, no `feme.stage.*`/`feme.image.*` operation, no `CompiledStage`,
no image or sampler descriptor, and no `lib/Graphics`, `lib/RayTracing`,
`lib/Vulkan` or `lib/Direct3D` directory of any kind. Its priorities are
relative *within that track*: a P0 there does not outrank a P1 in §1.1–§1.7,
it means "the graphics/runtime work stalls until this lands".

The two tracks are not independent. Four compute-track narrowings recorded in
§1.6 are load-bearing prerequisites for graphics milestones
(FeMeGraphicsDesign.md, "Prerequisites from the compute CPU target"), and
§1.6's "Dispatch is sequential, not thread-pooled" row is the *first* thing
both API runtimes need. §1.8's table records which is which.

## Part 1: Gap inventory

### 1.1 Core library plumbing

The library-shape parts of Design.md are the least finished part of FeMe,
because every milestone so far has been able to route around them.

| Gap | Owner section | Priority |
|---|---|---|
| `Context` registers no MLIR dialects (`lib/Core/Context.cpp` still carries the `TODO`; every tool registers its own dialects instead) | "`feme::Context`" | P1 |
| No `FormatRegistry`; `Driver` hard-codes `detectFormat` (done by R11: `feme::FormatRegistry` maps format names to `Importer`/`Exporter` instances, populated by `Driver`'s constructor rather than `Context`'s own -- see the Deviation note this adds to "`feme::Context`" -- and `detectFormat` now looks each Importer up in it) | "Status: `feme::Driver`" | P1 |
| No `Exporter` interface exists at all — the symmetric half of `Importer` was never written; DXIL/SPIR-V "export" is spelled as a `Backend` today (done by R11: `feme::Exporter`/`ExportOptions` plus `feme::DXILExporter`/`feme::SPIRVExporter`, each resolving the same target triple `Driver::resolveTargetTriple` already computes and delegating to `TargetMachineBackend`) | "`Exporter`" | P1 |
| `Context` has no `setDiagnosticHandler`/`diagnose` at all (`include/feme/Core/Context.h` exposes only the two context accessors); fallible library code returns `llvm::Error` and each tool prints it itself (done by R11: `feme::Diagnostic`/`Context::setDiagnosticHandler`/`diagnose` now carry `Driver::run`'s `--wave-size`-ignored warning, the one library-code `errs()` write that existed, with `feme`/`feme-run` each installing their own stderr handler) | "Diagnostics and Error Handling" | P1 |
| Thread-safety (one `Context` per thread, stateless components) is a stated invariant with no test asserting it (done by R11: `unittests/Driver/ThreadSafetyTest.cpp`) | "Core Architectural Principle" | P0 |
| C API | Design.md milestone 10 | P2 |

The thread-safety item was P0 because it is a *claim in the design*, made
about a library whose primary use case is a multi-threaded driver, that
nothing verified. `ThreadSafetyTest.cpp` drives one shared, stateless
`DXILImporter` plus the DXIL raising passes from N threads, each with its
own `Context`, and checks every `Context`'s underlying
`LLVMContext`/`MLIRContext` stays distinct while all are simultaneously
alive (run it under TSan for the stronger form of this check).

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
- **P1 — SPIR-V → DXIL direction (done by R13).** Design.md milestone 6's
  remaining half: `feme::dxil::SPIRVRaisingPass` raises the `llvm.spv.*`/
  `target("spirv.")` conventions back to `llvm.dx.*`/`target("dx.")`,
  covering the thread/group index queries and a `StorageBuffer` resource
  accessed through a flat element access. A typed-buffer image resource
  remains unraised, since it is still blocked on the conversion breadth
  above (no SPIR-V shader reading/writing one reaches LLVM IR at all yet).
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
- **P1 — `BinaryWriter` (`feme::dxsa::serialize`, done by R13)** implements
  the DXBC export path's hard prerequisite: it reuses `dxbc-as`'s own
  mnemonic-to-opcode table and encoder (`lookupOpcode`/`getOpcodeInfo`/
  `encodeProgram`) and covers every operation built from DXSAOpBase.td's
  five generic shapes (no-operand/unary/binary/ternary/multiply-add) plus
  `DXSA_MovConditionalOp`'s `movc`/`dmovc` family -- the ISA's arithmetic/
  logic/comparison/conversion core. Declarations, control flow, and
  resource/texture ops still diagnose rather than serialize, so a real,
  complete shader cannot round-trip through it end to end yet.
- **P1 — `translateToLLVMIR` coverage**, in the dependency order its own
  status note gives: resource queries (`bufinfo`/`resinfo`/`sampleinfo`/
  `samplepos`), atomics and UAV counters, groupshared memory, doubles,
  subroutines (`label`/`call`), non-pixel-shader stage declarations (which
  includes the `NumThreads` gap R7 found, above).
- **P2 — SM5 opcode coverage** in the dialect/parser itself, incrementally.

### 1.5 Retargeting

- **P1 — NVPTX and AArch64 (done by R13)** (Design.md milestone 9's
  remainder). `feme::nvptx::RaisedLoweringPass`/`ResourceLoweringPass` are
  the NVPTX counterparts to AMDGPU's own pair, mapping the same raised
  conventions onto NVVM/PTX-kernel primitives; unlike AMDGPU, NVPTX has no
  native object-file codegen (only PTX assembly text) and
  `feme::Backend`'s `BackendOptions::FileType` has no knob yet to request
  that, so there is no end-to-end `feme --target=nvptx*` object-file test,
  only the two passes' own `feme-opt` coverage. `Driver`'s triple
  resolution turned out to already be generic enough for AArch64 with no
  code changes at all -- `test/Tools/feme/feme-dxil-to-aarch64.ll` is a new
  end-to-end test proving that against a genuine non-host CPU ISA, not a
  new capability.
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
| Barrier inside a surviving *branch* (not a loop; R5 closed the loop case) is diagnosed, not split | 9 | P1 |
| Divergent groupshared access is diagnosed (including a groupshared `atomicrmw`/`load`/`store` reached through a `getelementptr`, even one every index of which is constant, and a *masked* store even at a uniform address -- found by R5's `reduction.hlsl` -- only a *direct*, unconditionally-executed global reference, with no intervening `getelementptr` at all, is canonicalized as of R2; see `feme/test/Transforms/CPU/simdize-groupshared-atomic-scalar.ll`'s comment) | 9 | P1 |
| Vector/aggregate leaf decomposition narrower than the design (R12 added a vector-typed resource load producer and a constant-index `extractelement` consumer; a divergent `shufflevector`/`phi`/`select` of vector type, a non-constant-index `extractelement`, and every aggregate remain diagnosed) | 7 | P1 |
| Masked memory always lowers to `gather`/`scatter` (no uniform/contiguous cases) | 7 | P2 (perf) |
| `Device` vs `All` memory scope not distinguished | 9 | P2 |
| Dispatch is sequential, not thread-pooled | 4 | P1 |
| `feme-run`'s heap YAML has no `class`/`kind`/`stride`/`format` | 11 | P0 (see §2.4) |

R12 closed this table's "Root constants unimplemented" row (`feme::cpu::RootConstantLoweringPass`, see its own roadmap entry) and "`WaveReadLaneAt`'s lane operand assumed uniform" row (`feme::cpu::WaveLowering.cpp`'s `lowerReadLane` now builds a genuine per-lane gather; SPIR-V's shuffle-style read, the only form with no HLSL uniform-index guarantee, is the one that can actually be divergent).

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
  an ASan/UBSan `check-feme` configuration is the systematic version. It is
  also a stated requirement of all three new designs, each of which adds
  attacker-controlled parsers (SPIR-V from applications, descriptor updates,
  command streams, pipeline-cache blobs, acceleration-structure builds), so
  it stops being a nice-to-have the moment §1.9/§1.10 start.

### 1.8 Graphics core and the CPU stage ABI

Owned by [FeMeGraphicsDesign.md](FeMeGraphicsDesign.md). None of it exists.
This section is the shared half of §1.9 and §1.10: every row here blocks both
API runtimes, which is why it is scheduled ahead of either.

#### 1.8.1 Compute-track prerequisites the graphics design depends on

These are already in §1.1–§1.7 as compute gaps. They are repeated here only
because the graphics design's "Prerequisites from the compute CPU target"
table makes them blocking, which changes their priority: each is P1 in §1.6
for compute and P0 for the track below.

| §1.6 narrowing | Blocks | Priority here |
|---|---|---|
| Dispatch is sequential, not thread-pooled (`JITEngine` has no unit of work smaller than a whole dispatch; `JITOptions::NumThreads` is accepted and ignored) | Every runtime milestone — Vulkan V1, Direct3D W1, and graphics G1 all need `CompiledStage::invokeGroup` | P0 |
| `ArtifactInfo`'s `WaveSize`/`GroupSize`/`GroupSharedSize`/`GroupSharedAlign` are in the version-2 layout but always written as 0 (see ResourceInfo.h's own note) | V1, W1 — a runtime cannot size a workgroup or its groupshared block from reflection | P0 |
| Divergent groupshared access is diagnosed | V2, W2 (an `SV_GroupIndex`-indexed `groupshared` array is what ordinary shaders write), G5, G6 | P0 |
| A barrier inside a surviving *branch* is diagnosed, and a `phi` live across a group-sync barrier cannot be spilled | G5, G6 — a tessellation-control stage that cannot synchronize inside control flow cannot express its source model | P1 |
| Root constants cover only the default `(b0, space0)`, non-array `dx.CBuffer`, constant-row-index shape R12 landed | V3's full advertised `maxPushConstantsSize`, W2's CBVs, G1's `FemeShaderResources::RootConstants` | P1 |

The last row is a *correction* to all three new documents: FeMeWARPDesign.md's
status section says "Root-constant lowering does not exist,
`ResourceInfo::RootConstantSize` is always zero", and
FeMeGraphicsDesign.md's prerequisite table lists root constants as an
unsupported resource kind. Both predate R12, which landed
`feme::cpu::RootConstantLoweringPass` and made `ResourceInfo::RootConstantSize`
real (`lib/Target/CPU/ResourceInfo.cpp`). What is actually left is breadth,
not existence, which is why this is P1 rather than P0.

#### 1.8.2 Core reflection and canonical graphics IR (G0)

| Gap | Owner section | Priority |
|---|---|---|
| No `feme::ShaderStage` enumeration and no `feme.shader.stage` entry-point attribute; CPU stage selection is `feme::cpu::PreparePass`'s `isComputeEntryPoint` string comparison against `"compute"` | "Stage identity" | P0 |
| No signature reflection of any kind: no element ID, direction, location, semantic, system value, component type, shape, interpolation, frequency, or stream | "Signature reflection" | P0 |
| `feme::dxil::MetadataRaisingPass` erases `!dx.entryPoints` — including the input, output, patch-constant and root-signature rows — after recovering only `hlsl.shader`/`hlsl.numthreads`/`hlsl.wavesize` | "Signature reflection" | P0 |
| SPIR-V conversion deliberately fails to legalize non-builtin `Input`/`Output` variables, and converts no `Location`/`Component`/`Index`/interpolation/per-primitive/per-patch decoration | "Signature reflection"; §1.2's own "graphics stage inputs/outputs" gap | P0 |
| No canonical stage operations (`feme.stage.input.load`/`output.store`/`discard`/`demote`/`is_helper`/derivative/quad/interpolate/emit/cut/mesh/ray families) and no `lib/Transforms/Graphics` canonicalization or validation pass | "Canonical stage operations" | P0 |
| No `StageInterfaceMap` or cross-stage linkage validation | "Signature reflection" | P1 |

DXIL's `loadInput`/`storeOutput` being unraised is *already* recorded as a
compute-track gap (§1.4's R7 entry notes no DXBC graphics-stage shader is
retargetable because of it, and that real DXIL input shares the gap). G0 is
where it is finally owned by a design rather than noted as a limitation.

#### 1.8.3 CPU stage compilation (G1)

| Gap | Owner section | Priority |
|---|---|---|
| `runPipeline(llvm::Module &, llvm::StringRef, unsigned)` has no stage parameter and no `StageCompileOptions` | "CPU Lowering Pipeline" | P0 |
| No `CompiledStage`/`PreparedDispatch`/`invokeGroup` — the type FeMeVulkanDesign.md calls `CompiledKernel` and FeMeWARPDesign.md asks to share; the graphics design's answer is that there is exactly one type, so V1/W1 should build against the final name | "Compiled stage API" | P0 |
| `feme::cpu::EntryWrapperPass` emits only `feme_cpu_entry_<name>(const FemeDispatchArgs *)`; there is no vertex, fragment, patch, mesh or ray continuation wrapper | "Vertex wrapper" … "Ray continuation wrappers" | P0 |
| One implicit active mask controls both execution and stores; fragment execution needs a separate live mask and side-effect mask, with `discard` clearing both and `demote` clearing only the second | "Shared middle-end phases" | P0 |
| No derivative or quad lowering at all (`QuadOp` is raised but not lowered — §1.3's R4 entry — and FeMeCPUDesign.md's Non-Goals still defer the lane-to-quad mapping a fragment stage requires at wave sizes 4 and 8) | "Derivatives and quad operations" | P0 |
| No `FemeStageLayout`, `FemeVertexArgs`, `FemeFragmentArgs` or any stage argument block | "Graphics Runtime ABI" | P0 |
| `ArtifactInfo` is compute-shaped; there is no stage-tagged `StageArtifactInfo` carrying signatures, side-effect summaries, tessellation/mesh/ray layouts | "Artifact reflection" | P1 |

G1 is the design's own discriminating milestone: if a vertex or fragment
shader cannot pass through the existing uniformity, linearization, SIMDization
and wave-lowering phases with localized extensions, the shared middle-end
boundary is wrong and must be revised before any fixed function is built. It
should therefore be treated as a decision point, not just another step.

#### 1.8.4 Images, samplers, and formats (G2)

| Gap | Owner section | Priority |
|---|---|---|
| `FemeDescriptor` cannot express dimensionality, mip/array ranges, sample or plane layout; `ResourceKind` is `{None, Typed, Structured, Raw, CBuffer}` with no image kind | "Separate descriptor kinds" | P0 |
| `FemeDispatchArgs::SamplerHeap` is typed `const FemeDescriptor *` — reserved before sampling had any representation — and must become `const FemeSamplerDescriptor *` | "Relationship to the compute ABI" | P0 |
| No `FemeShaderResources` block shared by compute and graphics; the resource fields are inlined into `FemeDispatchArgs` | "Relationship to the compute ABI" | P0 |
| No `feme.image.*`/`feme.sampler.*` operations and no sampling, filtering, mip-selection, addressing-mode, sRGB or format-conversion helpers in `runtime/CPU` | "Canonical image operations", "Texture layout and formats" | P0 |
| DXIL texture/sampler handle kinds are unraised (§1.3's own P1 row, blocked on recovering dimension/multi-sample/feedback bits) and SPIR-V sampling beyond basic `ImageSampleImplicitLod`/`OpImageFetch` is unconverted (§1.2's R9 entry) | §1.2, §1.3 | P0 |

G2 is scheduled before any raster stage on purpose: compute shaders that
sample images are required by Vulkan V5 and Direct3D W3, both of which precede
graphics in their own documents. It is also the ABI break — compiled artifacts
produced before it stop loading, which the design accepts explicitly now and
would not later.

#### 1.8.5 Software graphics and ray executors (G3–G8)

Everything here is greenfield: `FeMeGraphics`, `FeMeRayTracing`, the
normalized pipeline and prepared-draw descriptions, vertex/index fetch,
clipping, viewport transform, culling, tile binning, coverage, interpolation,
depth/stencil, blending, MSAA, tessellation, meshlets, acceleration-structure
builds and traversal, and the ray continuation transform. Priorities inside
this group are the milestone order itself (G3 → G8); nothing else depends on
them.

Two constraints from the design are worth restating because they are easy to
lose in scheduling:

- Neither runtime may advertise a graphics-capable queue,
  `VK_QUEUE_GRAPHICS_BIT`, or a raster-implying Direct3D feature level until
  the corresponding G milestone's completion test passes for every format and
  state combination it reports. Partial graphics support is worse than none.
- Wavefront packetization of ray continuations (G8) is only allowed *after*
  scalar continuation execution exists as the differential reference.

Work graphs are explicitly a later, separate design, not an extension of
amplification fanout or the ray continuation queues.

### 1.9 Vulkan runtime

Owned by [FeMeVulkanDesign.md](FeMeVulkanDesign.md). Nothing exists: there is
no `lib/Vulkan`, no manifest, no `vk.xml` generator, and no external
dependency machinery of any kind.

| Gap | Owner section | Priority |
|---|---|---|
| Vulkan-Headers would be FeMe's **first external dependency**. FeMe is built in-tree only, with no optional external package pattern to copy: the configuration surface, the disabled-path CI coverage, and the `vk.xml` version floor are new project-wide obligations | "Project and Library Boundaries" | P0 |
| No generated entrypoint table; hand-maintaining command names, aliases, core-version promotions and extension guards is the failure mode the design rejects | "Loader Integration" | P0 |
| Symbol visibility and LLVM coexistence: the loader loads *every* ICD into the process, so `libfeme_vulkan.so` shares an address space with Mesa drivers linking their own LLVM. Static LLVM/MLIR, `-fvisibility=hidden`, an exports version script, no `llvm::cl` registration on any reachable path, and one-shot target initialization under `std::once_flag` are hard requirements, verified by a two-ICD test and an exported-symbol-set link check | "Process Coexistence and Symbol Visibility" | P0 |
| **`feme::SPIRVImporter` cannot ingest realistic Vulkan SPIR-V at all.** It wraps `mlir::spirv::deserialize`, whose structurized reconstruction rejects an `OpPhi` in a loop merge block — which any loop carrying a value-producing `break` emits — and has been observed to fail on `OpCopyObject`. Only trivial control flow imports today | "SPIR-V import prerequisites" | P0 |
| No SPIR-V binding-to-heap normalization: `feme::cpu::BoundResourceNormalizationPass` rewrites DXIL's `handlefrombinding` only, and R10's `SPIRVResourceLoweringPass` normalizes a *single* bound storage buffer directly, with no descriptor-set, arrayed-binding or dynamic-offset model | "Required SPIR-V resource work"; §1.2 | P0 |
| Everything else in the object model — instance/device/queue, memory, buffers, descriptor pools/sets/updates, command pools and buffers, submission, fences, binary and timeline semaphores, events, query pools, pipeline cache | V0–V4 | P1 |
| Images, image views, layout tracking, copies, storage/sampled images and samplers | V5 | P1 (blocked on G2) |
| Graphics, WSI and presentation: **V6–V8 do not exist in FeMeVulkanDesign.md.** The graphics design supplies their FeMe-side content and lists what they unblock, but the Vulkan-side milestones — graphics queue family, `VkRenderPass`/dynamic rendering, graphics pipeline state, and the WSI decision — still have to be written | "Sequencing against the API runtime designs" (Graphics) | P1 (documentation) |

The SPIR-V import row is the largest single unknown in the Vulkan design, and
it is scheduled as its own milestone (V0.5) *before* V1 precisely because its
outcome — fixing MLIR's deserializer upstream versus translating the SPIR-V
CFG directly to unstructured LLVM IR and leaning on `feme::cpu::PreparePass`'s
existing structurizer — may change V1's design. It is also the one row here
that is a *FeMe* gap rather than a runtime gap, so it stays owned by this
roadmap even though the Vulkan document schedules it.

### 1.10 Direct3D software adapter

Owned by [FeMeWARPDesign.md](FeMeWARPDesign.md). Nothing exists.

| Gap | Owner section | Priority |
|---|---|---|
| The integration boundary is undecided: whether a software/render-only adapter can be installed and enumerated through DXGI at all, versus an application-local compatibility runtime. Choosing wrong invalidates most object-layer work, so W0 is explicitly a throwaway-capable prototype and gates every Windows-facing line of code | "Replacement and Deployment Model", W0 | P0 |
| No Windows SDK/WDK/DDI version selection, signing, INF, CI or debugging story — and no Windows CI in this tree at all | W0 | P0 |
| Same in-process LLVM coexistence requirement as §1.9 (the adapter loads into processes that may already host DXC), plus an exported symbol set checked against the selected DDI contract | "Project and Binary Boundaries" | P0 |
| Serialized root signatures of both versions, descriptor heaps and tables | W2 | P1 |
| Device/queue/allocator/list/fence/heap/buffer/pipeline objects, device-removal propagation, indirect dispatch, copies, barriers, queries | W1–W2 | P1 |
| Textures, sampling, copy footprints, views, format matrix | W3 | P1 (blocked on G2) |
| Raster, output merge, tessellation/geometry, pipeline libraries, persistent cache, HLK | W4–W5 | P2 (blocked on G3–G5) |
| DXGI presentation, shared resources/fences, D3D11-on-12 evaluation | W6 | P2 |

The Direct3D track needs no new milestones of its own — W4–W6 are already
scheduled — only the dependency on G2–G8 that the graphics design records.
Its portable half (command execution, resource layout, software graphics) is
required to stay unit-testable on non-Windows hosts, which is what makes W1–W5
partially reviewable in this tree at all; only `lib/Direct3D/Windows` is
genuinely Windows-only.

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
5. **Optimization level** (done by R14). Every end-to-end HLSL execution
   test JITs its shader through `feme::cpu::JITEngine`'s `OptimizerPipeline`
   pass, which has always run at a hardcoded `CodeGenOptLevel::Default`
   (`-O2`-equivalent, see `JITEngine.cpp`'s `toOptimizationLevel`) with no
   way for a test to pick a different level, so nothing ever checked that a
   shader's answer stays the same across levels. `feme-run` gains a `-O`
   flag (matching `llc`'s own spelling, `cl::Prefix` and all) that feeds
   `JITOptions::OptLevel` -- a field that already existed for this purpose
   but was never wired to the CLI; `test/Tools/feme-run/HLSL/opt-level-
   differential.hlsl` runs the same shader (a small unrolled per-lane
   multiply-add loop, with real reassociation/vectorization opportunities
   for the optimizer to find) at `-O0` through `-O3` and checks all four
   produce identical output. This is a different knob from `feme`'s own
   `-O0`/.../`-Od` (`feme::FrontendOptions`, which select the level the
   *front-end* pipeline runs its optimizer at, before any CPU-specific
   lowering) -- the two are independent, and this item is specifically
   about the JIT path's own post-CPU-lowering optimization pass.
6. **Round trips** (DXIL→DXIL→run done by R14; SPIR-V→SPIR-V→run remains
   blocked, see below). `feme-dxil-to-dxil.ll` checks a DXIL→DXIL retarget
   produces a container, but no test re-imports the produced artifact and
   executes it. `test/Tools/feme-run/HLSL/dxil-roundtrip-execute.hlsl`
   closes this: real HLSL compiled to a DXContainer, retargeted back to
   `dxil` through the full `feme` CLI (a second pass through
   `feme::DXILImporter`/the raising passes/`feme::DXILExporter`), and the
   *result* of that round trip executed and `FileCheck`ed through
   `feme-run` -- a much stronger statement than one that only parses the
   output. The equivalent SPIR-V→SPIR-V→run test turns out not to be
   possible yet: `feme --target=spirv` on a SPIR-V-derived module retargets
   through `feme::SPIRVExporter`, which (unlike the hand-authored `spirv`
   dialect fixtures every existing SPIR-V test in this tree assembles with
   `feme-translate --serialize-spirv`) emits its binary through LLVM's own
   in-tree SPIR-V code generator (`feme::TargetMachineBackend`) -- and
   `feme::SPIRVImporter` (a thin wrapper around MLIR's `spirv::deserialize`,
   see Design.md's SPIR-V section) cannot parse that generator's own
   output back in (`error: unhandled opcode 83` -- `OpAccessChain` --
   deserializing a module that `feme-run` executes just fine *before* that
   round trip). This is a genuine incompatibility between two independently
   evolving upstream SPIR-V producers/consumers (LLVM's SPIR-V backend and
   MLIR's SPIR-V deserializer), not something this roadmap step's own scope
   (testing, not new import breadth) can or should fix; see the "Known gap"
   this adds to Design.md's SPIR-V section for the concrete repro.
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

### 2.6 Testing the graphics and API runtime track

§2.1–§2.5 are about the compute track's breadth. The new components need
their own layers, and three of them are infrastructure that must exist before
the first shader-facing milestone rather than alongside it.

#### 2.6.1 Infrastructure prerequisites

1. **An image resource class in `feme-run`'s heap YAML.** Today's schema
   accepts `resource-heap`/`bindings` entries with `kind`/`format`/`stride`
   (§2.4.3). An image entry additionally needs dimensionality, extent, mip and
   array ranges, format, and layout. Required by every G2 test and by the
   textual image fixtures G3 onward compare against.
2. **`feme-render`.** A new tool that renders a textual scene description to a
   textual image fixture through the graphics executor, the way `feme-run`
   dispatches a compute shader. It must be added to Design.md's tool list and
   given a `docs/CommandGuide/` page like every other FeMe tool. Needed from
   G3; the alternative — growing `feme-run` a draw mode — mixes two very
   different argument models.
3. **Textual scene and image fixtures.** §2.5's "no binary fixtures" rule
   applies unchanged: scenes, textures and expected images are text, generated
   or compared at test time. This is what makes edge-rule failures reviewable
   in a diff.

Two more become prerequisites once the runtimes start:

4. **Lit clients under `test/Vulkan/`** running against the build-tree
   manifest via `VK_DRIVER_FILES`, plus a second-ICD coexistence
   configuration and an exported-dynamic-symbol check against the version
   script (§1.9).
5. **A differential reference for each API.** Vulkan compares against Mesa's
   lavapipe, Direct3D against Microsoft's WARP, in both cases only for the
   subset each milestone actually advertises. Both are optional, detected
   dependencies: a missing reference must skip, never fail.

#### 2.6.2 Layers

The graphics design's six layers map onto this tree as:

| Layer | Where | First needed |
|---|---|---|
| Core import convergence (same canonical signature/operations from DXIL and SPIR-V) | `test/Transforms/Graphics/` | G0 |
| CPU pass tests at wave sizes 4, 8 and one native width, checking mask, quad, barrier, output-bound and spill invariants | `test/Transforms/CPU/Graphics/` | G1 |
| Runtime helper tests exhausting image coordinates, formats, filtering, robustness and helper-lane side effects | `unittests/Graphics/` | G2 |
| Fixed-function unit tests with no shader frontend (clipping, edge rules, interpolation, depth/stencil, blend, tessellator, meshlet validation, BVH build/traversal, layout math) | `unittests/Graphics/`, `unittests/RayTracing/` | G3 |
| End-to-end executor tests rendering textual scenes | `test/Tools/feme-render/` | G3 |
| Frontend tests creating real Vulkan/Direct3D pipelines | `test/Vulkan/`, `test/Direct3D/` | V1, W1 |

#### 2.6.3 Metamorphic properties

These are the checks that catch the failures image comparison alone does not,
and each should land with the milestone that makes it meaningful rather than
as a late sweep:

- identical wave-size-independent output across wave sizes (extends §2.2.1's
  existing sweep to stages);
- identical deterministic output across worker counts and tile traversal
  orders — the graphics counterpart of §2.2's differential harness, and the
  first thing a thread-pooled dispatch (§1.6) needs;
- identical linked varyings after irrelevant signature elements are added;
- identical sampling through storage-compatible API format aliases;
- no resource or attachment change from helper-only quads;
- the same canonical behavior from equivalent DXIL and SPIR-V inputs — the
  stage-shaped version of §2.2.3's front-end equivalence test;
- identical primitive streams from equivalent conventional and mesh pipelines;
- identical hits between BVH traversal and brute-force intersection;
- identical scalar and packetized ray continuation results.

#### 2.6.4 Fuzzing and sanitizers

§1.7's rule — "a fuzzing harness lands alongside each importer" — generalizes
to "alongside each attacker-controlled parser". The new ones are descriptor
updates, pipeline-cache blobs, command-stream decoding, serialized root
signatures, artifact/`StageArtifactInfo` blobs, and acceleration-structure
builds. Each belongs in `check-feme-fuzz` (§2.4.6) with its own seed corpus
the day its parser lands.

Sanitizer configurations stop being §1.7's P1 nice-to-have: ASan/UBSan for
import and raster code, TSan for prepared-draw and tile scheduling (and for
queue/pipeline concurrency in both runtimes), forced allocation/JIT failure
injection, and stress configurations that cap tessellation, mesh queues,
continuation memory and ray recursion at small values so every
resource-exhaustion path is exercised.

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
| R11 | Thread-safety test; route library diagnostics through `Context`; `FormatRegistry`; `Exporter` interface (done: `unittests/Driver/ThreadSafetyTest.cpp` drives one shared, stateless `DXILImporter` plus the DXIL raising passes from N threads, each with its own `Context`, and checks every `Context`'s underlying `LLVMContext`/`MLIRContext` stays distinct while all are simultaneously alive; `feme::Diagnostic`/`Context::setDiagnosticHandler`/`diagnose` replace `feme::Driver::run`'s direct `errs()` write for its `--wave-size`-ignored warning, with `feme`/`feme-run` each installing their own stderr handler; `feme::FormatRegistry` maps format names to `Importer`/`Exporter` instances, populated lazily by `feme::Driver`'s constructor rather than `Context`'s own -- see the Deviation note this adds to the "`feme::Context`" section of feme/docs/Design.md -- and now backs `feme::detectFormat`; `feme::Exporter`/`ExportOptions` plus `feme::DXILExporter`/`feme::SPIRVExporter` close the "`Exporter` was never written" gap, each a thin wrapper resolving the same target triple `resolveTargetTriple` already computes and delegating to the existing `TargetMachineBackend`, registered into the `FormatRegistry` and used by `Driver` for a `--target` of `dxil`/`spirv` specifically) | §1.1 | R7 (a third format makes the registry pay) |
| R12 | Root constants; `WaveReadLaneAt` with a varying lane; vector/aggregate decomposition (done: `feme::cpu::RootConstantLoweringPass` lowers the one recognized register-bound constant buffer -- `(b0, space0)` by default, matching "Root constants" -- into bounds-checked loads from the CPU ABI's root-constant block, closing a real gap in `feme::dxil::OpRaisingPass` along the way (`raiseCBufferLoadLegacy` raises `dx.op.cbufferLoadLegacy` into `llvm.dx.resource.load.cbufferrow.4`, the standard 32-bit-per-component row shape, which nothing raised before); a shader that also performs bindless resource access has that root-constant access finished by `feme::cpu::ResourceLoweringPass` instead, reusing its own already-added `root_constants`/`root_constant_size` parameters rather than colliding by name with a second pair (see RootConstantLowering.h's file comment for the two-pass split, and `feme::cpu::checkSupportedRaisedOps`'s updated tolerance for either). `feme::cpu::WaveLowering.cpp`'s `lowerReadLane` now builds a genuine per-lane gather (an unrolled lane loop) instead of assuming a uniform index and extracting lane 0; `feme::cpu::WaveTTIImpl` keeps DXIL's `WaveReadLaneAt` classified uniform (HLSL's own language rule), but SPIR-V's broader shuffle-style read is left to the generic operand-divergence rule, since it has no such guarantee. `feme::cpu::SIMDizePass`'s vector/aggregate decomposition ("Vectors become components, not nested vectors") now also accepts a vector-typed resource *load* as a producer and a constant-index `extractelement` as a consumer of either producer shape, in addition to the existing constant-index-`insertelement`-chain/resource-store pair; a non-constant-index `extractelement`, `shufflevector`, `phi`/`select` of vector type, and every aggregate remain diagnosed) | §1.6 P1 | — |
| R13 | SPIR-V → DXIL direction; `BinaryWriter`; NVPTX/AArch64 (done: `feme::dxil::SPIRVRaisingPass` raises the SPIR-V-derived `llvm.spv.*`/`target("spirv.")` conventions a `Translator` produces back into the `llvm.dx.*`/`target("dx.")` ones `feme::dxil::OpRaisingPass`'s own output already uses -- the mirror image of `feme::spirv::RaisedLoweringPass` -- covering the four thread/group index queries with a direct mapping and a bound `StorageBuffer` resource (`target("spirv.VulkanBuffer", ...)`, HLSL's `(RW)StructuredBuffer<T>`) accessed through a flat `getpointer` plus an ordinary load/store, raised into DXIL's `target("dx.RawBuffer", ...)` handle and `llvm.dx.resource.load.rawbuffer`/`store.rawbuffer`; a typed-buffer image resource stays unraised, since MLIR's `SPIRVToLLVM` conversion still has no patterns for image *access* ops (only types), so none reaches LLVM IR to raise. `feme::Driver` runs it whenever a SPIR-V-derived module retargets to DXIL, closing Design.md milestone 6 end to end (`test/Tools/feme/feme-spirv-to-dxil.mlir`); doing so exposed a real bug shared by `feme::Driver::resolveTargetTriple` and `feme::DXILExporter` -- both fell back to a stage-less `dxil-unknown-shadermodel6.5-library` triple for any non-DXIL-originated module, which LLVM's DirectX codegen rejects outright for a stage-specific op like `llvm.dx.thread.id` -- now fixed by recovering the real pipeline stage from the entry point's `hlsl.shader` attribute first, the same attribute a SPIR-V `Translator` already sets. `feme::dxsa::serialize` (`BinaryWriter.cpp`) is implemented too, closing the DXBC-export prerequisite: it reuses `feme::dxbc::lookupOpcode`/`getOpcodeInfo`/`encodeProgram` (the exact table/encoder `dxbc-as`'s own text assembler already builds) rather than re-deriving SM4/SM5's bit layouts, and covers every operation built from DXSAOpBase.td's five generic shapes (no-operand/unary/binary/ternary/multiply-add) plus `DXSA_MovConditionalOp`'s `movc`/`dmovc` family -- the arithmetic/logic/comparison/conversion core of the ISA; declarations, control flow, and resource/texture ops are not yet covered and diagnose cleanly instead of mis-encoding, matching this dialect's own "extend incrementally" precedent. NVPTX gets its own `feme::nvptx::RaisedLoweringPass`/`ResourceLoweringPass`, mapping the same raised conventions onto NVVM/PTX-kernel primitives instead of AMDGPU's -- narrower than AMDGPU's own coverage in one respect the design didn't anticipate: NVPTX has no native object-file (ELF) codegen, only PTX assembly text, and `feme::Backend`'s `BackendOptions::FileType` has no knob yet to request that instead of the hard-coded `ObjectFile` default, so (unlike AMDGPU) there is no end-to-end `feme --target=nvptx*` object-file test yet, only the two passes' own `feme-opt` coverage (`test/Transforms/NVPTX`). AArch64 needed no new code at all: `feme::Driver`'s triple resolution and `feme::cpu::runPipeline` were already triple-generic, so `test/Tools/feme/feme-dxil-to-aarch64.ll` simply exercises that against a genuine non-host CPU ISA instead of leaving it an untested claim) | §1.2, §1.4, §1.5 P1 | R9 |
| R14 | `-O2` end-to-end differential; execute-after-round-trip tests (done: `feme-run` gains a `-O` flag wired to `JITOptions::OptLevel`, and `test/Tools/feme-run/HLSL/opt-level-differential.hlsl` checks `-O0`-`-O3` all compute the same answer; `test/Tools/feme-run/HLSL/dxil-roundtrip-execute.hlsl` closes the DXIL half of §2.2.6 -- the SPIR-V half remains blocked on a genuine `feme::SPIRVImporter`/LLVM-SPIR-V-backend incompatibility this step found and documented, see §2.2.6's own updated entry and the "Known gap" it adds to Design.md) | §2.2.5, §2.2.6 | R8 |
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
