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
  bias/gradient/comparison/gather variants (each needs its own pattern
  supplying additional operands; explicit-LOD sampling with a lone `Lod`
  operand is covered by R30's `ImageSampleExplicitLodPattern`),
  `Uniform`-storage-class buffer
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
- **P1 — SPIR-V bound resources (done by R26).** `SPV_EXT_descriptor_heap`
  remains unraised, so `feme::cpu::BoundResourceNormalizationPass` still
  handles DXIL's `handlefrombinding` only, and `handlefromimplicitbinding`
  still has no raiser to produce it -- but SPIR-V's own bound-resource
  form no longer needs that raiser to normalize an *arrayed* binding: see
  R26 below and §1.9's SPIR-V binding-to-heap normalization row.

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
- **P1 — texture/sampler handle kinds.** The blocking decision is now
  recorded in Design.md's DXIL section ("Decision: texture and sampler handle
  kinds"): the dimension/multi-sample/feedback bits this entry said were
  missing are in fact carried by `ResourceProperties` (Word0's `ResourceKind`
  byte *is* the dimension; Word1 carries component type/count, sample count
  and feedback kind), so the raised handle types are LLVM's own
  `dx.Texture`/`dx.MSTexture`/`dx.FeedbackTexture`/`dx.Sampler`. What is
  actually left is implementation, plus two narrower gaps the decision names:
  the legacy `!dx.resources` path has no component count and must recover the
  texel width from access sites the way typed buffers already do, and
  UNORM/SNORM/packed element kinds stay unraised until G2's format table
  exists. Implemented by R30 for the bindless `handlefromheap`/
  `handlefrombinding` path (see Design.md's status note); the legacy
  `!dx.resources`-based texture/sampler path and UNORM/SNORM/packed formats
  remain future work.
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
| ~~Barrier inside a surviving *branch* (not a loop; R5 closed the loop case) is diagnosed, not split~~ (closed by R24: a uniform two-way branch with no merge-block phi is now split, not diagnosed; a merge-block phi, or a value live across a barrier within one arm, remain diagnosed) | 9 | P1 |
| ~~Divergent groupshared access is diagnosed~~ (closed by R23: a divergent index, an access through a `getelementptr`, and a masked store at a uniform address are all now canonicalized; only a *nested* `getelementptr` -- a groupshared array of arrays/structs -- remains diagnosed) | 9 | P1 |
| Vector/aggregate leaf decomposition narrower than the design (R12 added a vector-typed resource load producer and a constant-index `extractelement` consumer; a divergent `shufflevector`/`phi`/`select` of vector type, a non-constant-index `extractelement`, and every aggregate remain diagnosed) | 7 | P1 |
| Masked memory always lowers to `gather`/`scatter` (no uniform/contiguous cases) | 7 | P2 (perf) |
| `Device` vs `All` memory scope not distinguished | 9 | P2 |
| ~~Dispatch is sequential, not thread-pooled~~ (closed by R21) | 4 | P1 |
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
`feme::cpu::rewriteGroupSharedGlobals` does not recognize -- both closed
by R23.

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
| ~~Dispatch is sequential, not thread-pooled (`JITEngine` has no unit of work smaller than a whole dispatch; `JITOptions::NumThreads` is accepted and ignored)~~ (closed by R21: `feme::cpu::CompiledStage::invokeGroup` plus a real `JITOptions::NumThreads`) | Every runtime milestone — Vulkan V1, Direct3D W1, and graphics G1 all need `CompiledStage::invokeGroup` | P0 |
| ~~`ArtifactInfo`'s `WaveSize`/`GroupSize`/`GroupSharedSize`/`GroupSharedAlign` are in the version-2 layout but always written as 0 (see ResourceInfo.h's own note)~~ (closed by R22's first half: `feme::cpu::CompiledStage::getArtifactInfo` (JIT) and `feme::Driver`'s CPU retargeting path (AOT, via `emitArtifactGlobal`) both now populate every field from the same resolved wave size/thread-group size and the new `feme::cpu::getGroupSharedRequirements` (feme/include/feme/Transforms/CPU/GroupSharedInfo.h)) | V1, W1 — a runtime cannot size a workgroup or its groupshared block from reflection | P0 |
| ~~Divergent groupshared access is diagnosed~~ (closed by R23) | V2, W2 (an `SV_GroupIndex`-indexed `groupshared` array is what ordinary shaders write), G5, G6 | P0 |
| ~~A barrier inside a surviving *branch* is diagnosed, and a `phi` live across a group-sync barrier cannot be spilled~~ (closed by R24) | G5, G6 — a tessellation-control stage that cannot synchronize inside control flow cannot express its source model | P1 |
| ~~Root constants cover only the default `(b0, space0)`, non-array `dx.CBuffer`, constant-row-index shape R12 landed~~ (closed by R25: any single register-bound binding, array bindings, and a dynamic row/array index are now accepted, and the reported size is the binding's full advertised span) | V3's full advertised `maxPushConstantsSize`, W2's CBVs, G1's `FemeShaderResources::RootConstants` | P1 |

The last row was a *correction* to all three new documents: FeMeWARPDesign.md's
status section says "Root-constant lowering does not exist,
`ResourceInfo::RootConstantSize` is always zero", and
FeMeGraphicsDesign.md's prerequisite table lists root constants as an
unsupported resource kind. Both predate R12, which landed
`feme::cpu::RootConstantLoweringPass` and made `ResourceInfo::RootConstantSize`
real (`lib/Target/CPU/ResourceInfo.cpp`); R25 then closed the remaining
breadth gap the correction called out.

#### 1.8.2 Core reflection and canonical graphics IR (G0)

| Gap | Owner section | Priority |
|---|---|---|
| ~~No `feme::ShaderStage` enumeration and no `feme.shader.stage` entry-point attribute; CPU stage selection is `feme::cpu::PreparePass`'s `isComputeEntryPoint` string comparison against `"compute"`~~ (closed by R16) | "Stage identity" | P0 |
| No signature reflection of any kind: no element ID, direction, location, semantic, system value, component type, shape, interpolation, frequency, or stream | "Signature reflection" | P0 |
| ~~`feme::dxil::MetadataRaisingPass` erases `!dx.entryPoints` — including the input, output, patch-constant and root-signature rows — after recovering only `hlsl.shader`/`hlsl.numthreads`/`hlsl.wavesize`~~ (closed by R18) | "Signature reflection" | P0 |
| SPIR-V conversion deliberately fails to legalize non-builtin `Input`/`Output` variables, and converts no `Location`/`Component`/`Index`/interpolation/per-primitive/per-patch decoration | "Signature reflection"; §1.2's own "graphics stage inputs/outputs" gap | P0 |
| ~~No canonical stage operations (`feme.stage.input.load`/`output.store`/`discard`/`demote`/`is_helper`/derivative/quad/interpolate/emit/cut/mesh/ray families) and no `lib/Transforms/Graphics` canonicalization or validation pass~~ (closed for vertex/fragment by R20; the stream-emission/mesh-output/ray families remain open for their own milestones) | "Canonical stage operations" | P0 |
| No `StageInterfaceMap` or cross-stage linkage validation | "Signature reflection" | P1 |

DXIL's `loadInput`/`storeOutput` being unraised is *already* recorded as a
compute-track gap (§1.4's R7 entry notes no DXBC graphics-stage shader is
retargetable because of it, and that real DXIL input shares the gap). G0 is
where it is finally owned by a design rather than noted as a limitation.

#### 1.8.3 CPU stage compilation (G1)

| Gap | Owner section | Priority |
|---|---|---|
| ~~`runPipeline(llvm::Module &, llvm::StringRef, unsigned)` has no stage parameter and no `StageCompileOptions`~~ (closed by R27: a new `runPipeline(llvm::Module &, const feme::cpu::StageCompileOptions &)` overload selects by `feme::ShaderStage`, tagging its result with the compiled stage; the original signature is kept as the compute-only compatibility overload the design asks for) | "CPU Lowering Pipeline" | P0 |
| ~~No `CompiledStage`/`PreparedDispatch`/`invokeGroup` — the type FeMeVulkanDesign.md calls `CompiledKernel` and FeMeWARPDesign.md asks to share; the graphics design's answer is that there is exactly one type, so V1/W1 should build against the final name~~ (closed by R21, landed under the `CompiledStage` name; `create` still takes the existing compute-only `JITOptions` rather than `StageCompileOptions`, and the stage-specific `invokeVertices`/etc. methods do not exist yet — R27 lands `StageCompileOptions` itself and the stage-aware `runPipeline` it threads through, but `CompiledStage::create` is deliberately left on `JITOptions` until R28 builds the vertex/fragment wrappers that would give a non-compute `CompiledStage` something to dispatch) | "Compiled stage API" | P0 |
| `feme::cpu::EntryWrapperPass` emits only `feme_cpu_entry_<name>(const FemeDispatchArgs *)`; there is no vertex, fragment, patch, mesh or ray continuation wrapper | "Vertex wrapper" … "Ray continuation wrappers" | P0 |
| ~~One implicit active mask controls both execution and stores; fragment execution needs a separate live mask and side-effect mask, with `discard` clearing both and `demote` clearing only the second~~ (closed by R27: `feme::cpu::LinearizePass`'s `DiamondFlattener`/`LoopLinearizer` thread a `MaskPair{Live, SideEffect}` instead of a single scalar mask -- `feme.stage.discard` narrows both, `feme.stage.demote` narrows only `SideEffect`, `feme.stage.is_helper` reads `Live && !SideEffect` -- and `--reference` mode gets its own counterpart (a real conditional early return for `discard`, a per-invocation `helper` flag for `demote`/`is_helper`). Scoped, like the rest of milestone 6/7's masking, to the divergent-diamond/divergent-loop-exit shapes already supported; a stage-mask call inside an otherwise-uniform loop is diagnosed rather than mis-widened, and `--reference` does not yet suppress a `demote`d invocation's later side effects) | "Shared middle-end phases" | P0 |
| No derivative or quad lowering at all (`QuadOp` is raised but not lowered — §1.3's R4 entry — and FeMeCPUDesign.md's Non-Goals still defer the lane-to-quad mapping a fragment stage requires at wave sizes 4 and 8) | "Derivatives and quad operations" | P0 |
| No `FemeStageLayout`, `FemeVertexArgs`, `FemeFragmentArgs` or any stage argument block | "Graphics Runtime ABI" | P0 |
| ~~`ArtifactInfo` is compute-shaped; there is no stage-tagged `StageArtifactInfo` carrying signatures, side-effect summaries, tessellation/mesh/ray layouts~~ (closed by R22 for the shared, stage-independent core: `feme::cpu::StageArtifactInfo` (renamed from `ArtifactInfo`) now carries a `feme::ShaderStage` tag, a serialized `feme::EntrySignature` (`Signature`, empty until R28 makes `CompiledStage` itself stage-aware), and side-effect-summary `Flags` bits (`FEME_CPU_ARTIFACT_USES_DISCARD`/`_DEMOTE`/`_HELPER`, computed by `feme::cpu::computeSideEffectFlags`); `ArtifactAbiVersion` is bumped to 3, and both `feme::cpu::CompiledStage::getArtifactInfo` (JIT) and `feme::Driver`'s CPU retargeting path (AOT) build the same structure. Tessellation/mesh/ray layouts remain for R34/R35/R37, once those stages exist to describe) | "Artifact reflection" | P1 |

G1 is the design's own discriminating milestone: if a vertex or fragment
shader cannot pass through the existing uniformity, linearization, SIMDization
and wave-lowering phases with localized extensions, the shared middle-end
boundary is wrong and must be revised before any fixed function is built. It
should therefore be treated as a decision point, not just another step.

#### 1.8.4 Images, samplers, and formats (G2)

| Gap | Owner section | Priority |
|---|---|---|
| ~~`FemeDescriptor` cannot express dimensionality, mip/array ranges, sample or plane layout; `ResourceKind` is `{None, Typed, Structured, Raw, CBuffer}` with no image kind~~ (closed by R29: `FemeImageDescriptor` is its own descriptor type rather than a `ResourceKind` case, so `FemeDescriptor`/`ResourceKind` are unchanged and images never share their shape) | "Separate descriptor kinds" | P0 |
| ~~`FemeDispatchArgs::SamplerHeap` is typed `const FemeDescriptor *` — reserved before sampling had any representation — and must become `const FemeSamplerDescriptor *`~~ (closed by R29) | "Relationship to the compute ABI" | P0 |
| ~~No `FemeShaderResources` block shared by compute and graphics; the resource fields are inlined into `FemeDispatchArgs`~~ (closed by R29: `FemeDispatchArgs` now embeds a `FemeShaderResources Resources` member instead of declaring its own resource fields) | "Relationship to the compute ABI" | P0 |
| ~~No `feme.image.*`/`feme.sampler.*` operations and no sampling, filtering, mip-selection, addressing-mode, sRGB or format-conversion helpers in `runtime/CPU`~~ (closed by R30 for 2D images: DXIL's `dx.op.sample`/`sampleLevel`/`textureLoad` raise to LLVM's own `llvm.dx.resource.sample`/`samplelevel`/`load.level` -- the canonical, already target-generic-in-spelling intrinsics, not a bespoke `feme.image.*` dialect -- and `feme::cpu::ResourceLoweringPass` lowers a 2D `dx.Texture`/`dx.Sampler` pair's access into new `feme.cpu.image.sample.2d.v4f32`/`samplecmp.2d.f32`/`load.2d.v4f32` calls (`feme::cpu::ImageCalls`), implemented by `runtime/CPU/FeMeRuntimeCPU.c`'s point/bilinear filtering, all five addressing modes, explicit-LOD mip selection, PCF-style comparison sampling and an initial `R32G32B32A32_FLOAT`/`R8G8B8A8_UNORM(_SRGB)` format table with sRGB decode. 1D/3D/cube sampling, bias/gradient sampling, gather, active-lane SIMD widening for a *divergent* sample (a uniform sample already works), and CPU-side SPIR-V-sourced image lowering remain -- see "Canonical image operations"/"Texture layout and formats" in FeMeGraphicsDesign.md for the itemized list and why each is deferred) | "Canonical image operations", "Texture layout and formats" | P0 |
| ~~DXIL texture/sampler handle kinds are unraised (§1.3's own P1 row; the encoding is now decided in Design.md's "Decision: texture and sampler handle kinds", so only the implementation is left) and SPIR-V sampling beyond basic `ImageSampleImplicitLod`/`OpImageFetch` is unconverted (§1.2's R9 entry)~~ (closed by R30 for the bindless heap path and the DXIL ops LLVM's own backend already lowers a canonical intrinsic to (`Sample`/`SampleLevel`/`TextureLoad`/`GetDimensions.x`), plus SPIR-V's `ImageSampleExplicitLod`; comparison sampling/gather have no numbered DXIL wire opcode in this LLVM tree to raise from at all, an upstream gap, not FeMe's -- see Design.md's "Decision: texture and sampler handle kinds" status note) | §1.2, §1.3 | P0 |

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
| ~~No SPIR-V binding-to-heap normalization: `feme::cpu::BoundResourceNormalizationPass` rewrites DXIL's `handlefrombinding` only, and R10's `SPIRVResourceLoweringPass` normalizes a *single* bound storage buffer directly, with no descriptor-set, arrayed-binding or dynamic-offset model~~ (closed by R26: `feme::cpu::SPIRVResourceLoweringPass` now reads `llvm.spv.resource.handlefrombinding`'s own range-size and array-index operands rather than assuming an implicit range size of 1, assigning each (descriptor set, binding) identity a contiguous run of heap slots and range-checking a (possibly dynamic) array index into it exactly as `BoundResourceNormalizationPass` does for a DXIL array binding -- see that pass's updated header comment and feme/docs/FeMeCPUDesign.md's "Bound-resource normalization". A Vulkan *dynamic* storage/uniform buffer offset needs no shader-side model at all: per "Memory and Buffers" in feme/docs/FeMeVulkanDesign.md, it is folded into `FemeDescriptor::Data` when a host materializes a dispatch's heap, the same way every other buffer's binding offset is, so the existing `BoundResourceRange`/`materializeResourceHeap` model already carries it without a Vulkan-specific reflection record -- answering FeMeVulkanDesign.md's open questions 3 and 7) | "Required SPIR-V resource work"; §1.2 | P0 |
| Everything else in the object model — instance/device/queue, memory, buffers, descriptor pools/sets/updates, command pools and buffers, submission, fences, binary and timeline semaphores, events, query pools, pipeline cache | V0–V4 | P1 |
| Images, image views, layout tracking, copies, storage/sampled images and samplers | V5 | P1 (blocked on G2) |
| Graphics, WSI and presentation: FeMeVulkanDesign.md's V6–V8 (done: its "Graphics, Presentation, and Window-System Integration" section now specifies the graphics queue family, `VkRenderPass`/dynamic rendering normalized into one render-target binding, graphics pipeline state translation, draw commands, the headless-first WSI decision, and mesh/ray exposure, and V6–V8 are written against G3–G8) | "Graphics, Presentation, and Window-System Integration" (Vulkan) | P1 |

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
  `SV_GroupThreadID`) was blocked on §1.6's separate "Divergent
  groupshared access is diagnosed" row, which R5 found is one case broader
  than recorded (a *masked* store at a uniform address, not just a
  divergent index) -- both are now closed by R23.
- **`prefix-sum.hlsl`** (done by R4) — `WavePrefixSum`/`WavePrefixCountBits`
  over a divergent mask; exercises §1.3's flag-selected `WavePrefixOp`
  family.
- **`histogram.hlsl`** — divergent atomics into a shared buffer. This is the
  scalarization fallback's only realistic workload, and the one that catches
  §1.6's unmasked-lane P0 (done: a single groupshared counter a divergent
  condition gates `InterlockedAdd` into, reading the atomic's own return
  value rather than a separate reload -- a genuine multi-bucket histogram,
  indexing a groupshared array by a divergent bucket, was blocked on
  §1.6's separate "Divergent groupshared access is diagnosed" row (closed
  by R23): Clang itself folds an `if`/`else` each doing the same op on a
  different constant address into a single `select`-of-pointer
  `atomicrmw`, the address-divergent shape that narrowing covered).
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
   dispatches a compute shader. It is specified in Design.md's "Testing
   Tools" and has a [CommandGuide page](CommandGuide/feme-render.md);
   only the implementation is left. Needed from G3; the alternative — growing
   `feme-run` a draw mode — mixes two very different argument models.
3. **Textual scene and image fixtures.** §2.5's "no binary fixtures" rule
   applies unchanged: scenes, textures and expected images are text, generated
   or compared at test time. This is what makes edge-rule failures reviewable
   in a diff. Both formats are specified in Design.md's "Textual scene and
   image fixtures": one image format serves as texture input, expected output
   and actual-output dump, compared exactly by default, and a scene is YAML
   extending `feme-run`'s heap schema with attachments, pipeline state and
   vertex streams spelled in FeMe's own enumerations rather than any API's.

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

§3.1's table is the compute and retargeting track (R1–R15, the original
sequencing). §3.2 adds the graphics core and CPU stage work (R16–R37), and
§3.3 places the two API runtimes' own milestones against it. R16 onward may
proceed in parallel with R15, which has no dependents.

### 3.1 Compute and retargeting track

| # | Step | Covers | Depends on |
|---|---|---|---|
| R1 | Grow the differential harness to divergent/loop shapes; add the wave-size sweep (done: `--unstructured` stays `--reference`-only, see §1.6's new gap) | §2.2.1, §2.2.2, §2.4.1, §2.4.4 | — |
| R2 | Mask the scalarization fallback's per-lane execution; add `histogram.hlsl`; make `feme-cpu-simdize` reject every shape `feme-cpu-linearize` left an unwidened divergent branch in, including one inside a loop (§1.6's new gap, found by R1) (done: the divergent-branch gap turned out to be `feme::cpu::runPipeline` not propagating a pass diagnostic, not `feme-cpu-simdize`'s own check -- see §1.6's table) | §1.6 P0, §2.3 | R1 (harness catches regressions) |
| R3 | Multi-return-value raising mechanism (`IMul`/`UMul`/`UAddc`/`SplitDouble`/`WaveActiveBallot`) + `ballot.hlsl` (done: `feme::dxil::OpRaisingPass::raiseAggregateCall` raises all five; `feme::cpu::WaveCallKind::Ballot`/`lowerBallot` lower `WaveActiveBallot` on the CPU target) | §1.3 P0 | — |
| R4 | Flag-selected opcode families (`WaveActiveOp`/`WaveActiveBit`/`WavePrefixOp`/`QuadOp`/`Barrier`) + `prefix-sum.hlsl` (done: `feme::dxil::OpRaisingPass` raises all four remaining families; `feme::cpu::WaveLoweringPass` lowers every one of them except `QuadOp`, which stays raised-only pending the quad/derivative lane mapping FeMeCPUDesign.md's "Non-Goals" defers) | §1.3 P0 | — |
| R5 | Barriers inside a uniform loop; values live across barriers; `reduction.hlsl`, `multi-group-barrier.hlsl` (done: `feme::cpu::matchLoopShape`/`buildWrapperForLoop` split a barrier inside a header-tested uniform loop by cloning its header/latch into the wrapper as an ordinary scalar loop; `feme::cpu::spillValuesLiveAcrossBarriers` spills any value live across a barrier into a per-wave context array; a barrier inside a *branch* remains diagnosed, and a divergent groupshared access -- including a masked store at a uniform address, found writing `reduction.hlsl` -- was a separate narrowing, closed by R23, see §1.6) | §1.6, §2.3 | R4 (`Barrier` raising) |
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

### 3.2 Graphics core and CPU stage track

Every step here lands in `feme` proper — core reflection, the CPU target, and
the two new executor libraries — and is testable through `feme-opt`,
`feme-run`, `feme-render` and `gtest` without any API runtime existing. The
"Milestone" column is the FeMeGraphicsDesign.md milestone the step belongs to;
a milestone is complete when its own completion test passes, which is normally
the last step carrying its name.

| # | Step | Milestone | Covers | Depends on |
|---|---|---|---|---|
| R16 | `feme::ShaderStage` plus the `feme.shader.stage` entry-point attribute, derived at import from the source stage and diagnosed against the module triple's environment; `feme::cpu::PreparePass` selects by enumeration instead of `isComputeEntryPoint`'s string comparison, with `hlsl.shader` still accepted (done: `feme/include/feme/Core/ShaderStage.h` defines the enumeration, its canonical spellings, its mapping to and from the shader-stage triple environments a raised module already carries, and the attribute accessors -- `getShaderStage` preferring `feme.shader.stage` but still falling back to `hlsl.shader`, which import keeps writing because LLVM's own DirectX/SPIRV backends read the stage from it. `feme::dxil::MetadataRaisingPass` records the attribute and diagnoses an entry point whose `ShaderKind` property disagrees with the module's shader model profile; the `spirv` -> `llvm` dialect conversion records it and, since it *derives* the triple from the first entry point's execution model rather than checking against an authored one, diagnoses a module mixing two stages instead. `feme::cpu::PreparePass` takes a `feme::ShaderStage`, exposed as `feme-opt -feme-cpu-stage=<stage>`, so an entry point of another stage is a non-candidate rather than an ambiguity. See the "Status" note added to FeMeGraphicsDesign.md's "Stage identity" for the three decisions the design left open) | G0 | §1.8.2 | — |
| R17 | The signature reflection data model (element ID, direction, location, semantic, system value, component type, shape, interpolation, frequency, stream), its verifier, and its serialization round trip in `gtest` (done: `feme/include/feme/Core/Signature.h` defines `feme::SignatureElement`/`feme::EntrySignature` and the enumerations for each field; `feme::verifySignature` checks unique element IDs, an in-range component shape, a supported bit width, a semantic index only alongside a semantic name, and patch direction/frequency agreement; `feme::serializeSignature`/`feme::parseSignature` round-trip it through a versioned byte layout, following `feme::cpu::ArtifactInfo`'s convention. `unittests/Core/SignatureTest.cpp` covers the verifier's acceptance and each rejection rule plus the serialization round trip, including truncated/trailing-byte/unknown-enumerator failures. Import wiring (DXIL rows into the model, SPIR-V variables into the model) is left to R18/R19; see the "Status" note added to FeMeGraphicsDesign.md's "Signature reflection") | G0 | §1.8.2 | R16 |
| R18 | Preserve DXIL input/output/patch-constant/root-signature rows from `!dx.entryPoints` into that model before `feme::dxil::MetadataRaisingPass` erases the source metadata (done: `feme::dxil::convertEntrySignature` (`feme/include/feme/Transforms/DXIL/SignatureImport.h`) converts an entry's `Signatures` tuple into a `feme::EntrySignature`, renumbering `ElementID` by combined position since DXIL numbers its input/output/patch-constant lists independently; `feme::dxil::MetadataRaisingPass` calls it and attaches the result as `!feme.signature` function metadata, and separately preserves an entry's `EntryRootSigTag` (12) root-signature bytes verbatim as `!feme.dxil.rootsignature` function metadata (`setRootSignature`/`getRootSignature`) -- parsing the root signature's contents is left to W2. `unittests/Transforms/DXIL/SignatureImportTest.cpp` covers the element conversion (system value, component type, interpolation-mode mapping, patch direction by stage) and both metadata round trips; `test/Transforms/DXIL/dxil-raise-metadata-signature.ll`/`dxil-raise-metadata-patch-constant.ll` cover the pass end to end. SPIR-V's `Input`/`Output` variables (R19) are unaffected; see the "Status" note added to FeMeGraphicsDesign.md's "Signature reflection") | G0 | §1.8.2 | R17 |
| R19 | Convert SPIR-V non-builtin `Input`/`Output` variables and their `Location`/`Component`/`Index`/interpolation/per-primitive/per-patch decorations instead of failing to legalize them (done: `feme::spirv::populateSPIRVToLLVMTargetTypeConversions` converts a non-builtin `Input` variable's pointer to its pointee type (collapsing its `spirv.mlir.addressof`+`spirv.Load` into one `llvm.load`, like a builtin's `llvm.spv.*` intrinsic result already does) and a non-builtin `Output` variable's to an ordinary pointer in address space 8; `StageIOGlobalVariablePattern`/`StageIOAddressOfPattern` (`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`) convert the declaration itself to an `llvm.mlir.global` in address space 7/8, recording `Location`/`Component`/`Index`/`NoPerspective`/`Flat`/`Patch`/`Centroid`/`Sample`/`PerPrimitiveEXT` as a `feme.spirv.decorations` attribute that `feme::spirv::attachStageIODecorations` (`feme/lib/Conversion/SPIRVToLLVM/StageIODecorations.cpp`, called from `feme::SPIRVToLLVMTranslator::translate`) turns into real `!spirv.Decorations` metadata once a genuine `llvm::Module` exists -- the same shape LLVM's SPIRV backend reads `OpDecorate`s back from, verified through `llc`+`spirv-val`. `test/Conversion/SPIRVToLLVM/spirv-to-llvm-stage-io.mlir`/`test/Translate/SPIRV/spirv-to-llvmir-stage-io.mlir` and `unittests/Conversion/SPIRVToLLVM/SPIRVToLLVMTest.cpp` cover it; feeding these variables into the `feme::EntrySignature` model (R17) itself is left to R20, which is what actually consumes it -- see the "Signature reflection" Status note this adds to FeMeGraphicsDesign.md) | G0 | §1.8.2, §1.2 | R17 |
| R20 | The `feme.stage.*` operation family for vertex/fragment (input load, output store, discard, demote, is_helper, derivatives, quad read, pull-model interpolation) plus `FeMeTransformsGraphics`' canonicalization and validation pass, rewriting DXIL `loadInput`/`storeOutput` and SPIR-V interface accesses into it. **Completes G0** (done: `feme/include/feme/Core/StageOps.h` declares the `feme.stage.*` family as named calls, mangled per overload like DXIL's own `dx.op.*` convention; a new `FeMeTransformsGraphics` library (`feme/lib/Transforms/Graphics`) adds `feme::graphics::CanonicalizeStagePass` (`feme-graphics-canonicalize-stage`), which raises DXIL's `loadInput`(4)/`storeOutput`(5)/`IsHelperLane`(221)/the pull-model interpolation family (`EvalCentroid`/`EvalSampleIndex`/`EvalSnapped`) directly -- resolving each call's DXIL per-list signature ID through the entry's `!feme.signature` -- and renames the already-raised `llvm.dx.discard`/derivative/quad-read intrinsics into their `feme.stage.*` peers; on the SPIR-V side it rewrites a non-builtin `Input`/`Output` stage-IO global's load/store into `feme.stage.input.load`/`output.store`, building and attaching the `feme::EntrySignature` R19 deferred to this milestone, and renames the analogous `llvm.spv.*` intrinsics the same way. `feme::graphics::ValidateStagePass` (`feme-graphics-validate-stage`) diagnoses a non-constant/unknown/wrong-direction element or out-of-range row/component, and any stage op illegal for its entry's declared stage. Both are scoped to the vertex and fragment stages only, per the design's "only operations required by implemented stages are legal" -- the patch/stream-emission/mesh-output/ray families and SPIR-V `demote`/`is_helper` (no upstream `llvm.spv.*` intrinsic exists to raise from yet) are left to later milestones; see the "Status" note added to FeMeGraphicsDesign.md's "Canonical stage operations". `unittests/Transforms/Graphics/{CanonicalizeStage,ValidateStage}Test.cpp` and `test/Transforms/Graphics/*.ll` cover both passes) | G0 | §1.8.2, §1.4 | R18, R19 |
| R21 | Factor `CompiledStage`/`PreparedDispatch`/`invokeGroup` out of `JITEngine`, with the wave loop and entry mask owned by `invokeGroup`; `JITEngine` becomes a convenience wrapper and `JITOptions::NumThreads` becomes real. Land it under the final `CompiledStage` name so V1/W1 never build against `CompiledKernel` (done: `feme::cpu::CompiledStage` (`feme/include/feme/Target/CPU/CompiledStage.h`) now owns everything `JITEngine::create` used to -- module cloning, wave-size resolution, the CPU pipeline/reference lowering, linking `libFeMeRuntimeCPU`, JIT compilation, and entry-point resolution -- and exposes `invokeGroup(const PreparedDispatch &, GroupID, GroupShared) const`, the per-workgroup entry point both FeMeVulkanDesign.md's `CompiledKernel` sketch and FeMeGraphicsDesign.md's `CompiledStage` describe (see FeMeGraphicsDesign.md's "there is one type"); `feme::cpu::PreparedDispatch`/a free `invokeGroup` (`feme/include/feme/Target/CPU/ResourceHeap.h`) materialize a dispatch's physical heap once and build one group's `FemeDispatchArgs`, shared by both `CompiledStage::invokeGroup` and the AOT `runDispatch` path. `JITEngine` is now exactly the thin wrapper the design describes: it holds a `CompiledStage` and (when `JITOptions::NumThreads != 1`) an `llvm::DefaultThreadPool` sized by `llvm::hardware_concurrency(NumThreads)`, created once and owned for the engine's lifetime; `dispatch` schedules every group across it through a per-call `llvm::ThreadPoolTaskGroup` and joins, or runs sequentially on the calling thread with no pool at all when `NumThreads == 1`. Deviation: `invokeGroup` does *not* own a separate host-side wave loop as this row and FeMeVulkanDesign.md's sketch describe -- `feme::cpu::EntryWrapperPass` already builds that loop, with its own entry-mask computation, *inside* the compiled `feme_cpu_entry_<name>` wrapper, precisely so it can also split a barrier into separate wave loops and spill values live across it (milestone 9, predating this row); moving it to the host would mean either duplicating that machinery or re-deriving barrier correctness against a changed entry-point ABI, neither of which this row's actual gap (a dispatch's unit of work smaller than the whole thing) required solving. `invokeGroup` therefore calls the compiled entry point exactly once per group; see the Status notes this adds to FeMeVulkanDesign.md's "CPU Runtime API Changes" and FeMeGraphicsDesign.md's "Compiled stage API" for the full accounting, including `StageCompileOptions`/`getStage()`/the stage-specific `invoke*` methods being left to R27. `unittests/Target/CPU/CompiledStageTest.cpp` covers `invokeGroup` directly, including concurrently from several threads for independent `GroupID`s; a new `JITEngineTest` case covers `dispatch`'s real worker-pool path end to end; existing `JITEngineTest`/`AOTDispatchTest`/`ResourceHeapTest` coverage is unchanged and still passes) | G1 (shared with V1, W1) | §1.6, §1.8.1, §1.8.3 | — |
| R22 | Populate `ArtifactInfo`'s `WaveSize`/`GroupSize`/`GroupSharedSize`/`GroupSharedAlign`, then generalize it into stage-tagged `StageArtifactInfo` with signatures and side-effect summaries; bump the artifact ABI version and round-trip JIT and AOT reflection through the same structure (done: `feme::cpu::CompiledStage::getArtifactInfo` and `feme::Driver`'s CPU retargeting path both populate `WaveSize`/`GroupSize`/`GroupSharedSize`/`GroupSharedAlign` from the same already-resolved wave size/thread-group size and the new `feme::cpu::getGroupSharedRequirements` (feme/include/feme/Transforms/CPU/GroupSharedInfo.h); `ArtifactInfo` is then renamed `StageArtifactInfo` and gains a `feme::ShaderStage Stage` tag (default `Compute`, since every `CompiledStage` still is one), a serialized `feme::EntrySignature` `Signature` tail (empty until R27/R28 make compilation itself stage-aware), and side-effect-summary `Flags` bits (`FEME_CPU_ARTIFACT_USES_DISCARD`/`_DEMOTE`/`_HELPER`, computed by the new `feme::cpu::computeSideEffectFlags` scanning for `feme.stage.*` calls); `ArtifactAbiVersion` is bumped to 3. `unittests/Target/CPU/{ResourceInfo,CompiledStage}Test.cpp` cover the populated fields, the new struct fields' serialization round trip, and rejecting an unknown stage/truncated signature; `test/Tools/feme/feme-cpu-artifact-reflection.ll` covers the AOT path end to end through a real object file. Deviation: tessellation/mesh/ray layouts remain unaddressed, as they describe stages that do not exist yet (left to R34/R35/R37)) | G1 (shared with V1, W1) | §1.8.1, §1.8.3 | R17, R21 |
| R23 | Divergent groupshared access in `feme::cpu` — a divergent index, an access through a `getelementptr`, and a masked store at a uniform address (§1.6's three recorded shapes) (done: `feme::cpu::FunctionWidener::widenGroupSharedGEP` widens a divergent groupshared index into a real vector-of-pointers `getelementptr` (LLVM allows a scalar base with vector index operands) instead of the generic scalarization fallback's per-lane clone-and-reassemble via `insertelement`, which `feme::cpu::rewriteGroupSharedGlobals` could not see through; `widenGroupSharedLoad`/`widenGroupSharedStore` turn a raw (unmasked) divergent-address `load`/`store` into a real `llvm.masked.gather`/`.scatter`, masked only by the wave's own entry mask; `widenGroupSharedAtomicRMW` (replacing the unconditional `widenElementwise` call every `atomicrmw` used to get) and a matching fix to the existing `widenMaskedAtomicRMW` both reuse a *uniform* groupshared pointer -- most commonly a `getelementptr` at a compile-time-constant array index -- directly per lane instead of `getWidened`'s usual broadcast-then-extract, which (unlike a direct, unindexed global reference, a `Constant` `ConstantFolder` folds straight back to itself) would otherwise leave a real `insertelement`/`shufflevector` broadcast in place of the uniform address. `feme::cpu::rewriteGroupSharedGlobals` (GroupShared.cpp) is generalized to retarget every one of these shapes: a `getelementptr` may now be vector-typed; a gather/scatter call (`llvm.masked.gather`/`.scatter`, the only surviving shape once every `feme.cpu.masked.*` call has been widened away) is a recognized leaf alongside `load`/`store`/`atomicrmw`; and a new `matchPointerBroadcast` recognizes the same-value `<W x ptr>` broadcast a masked store at a *uniform* address (even a direct global reference, or a uniform `getelementptr`) still needs -- retargeting it by rebuilding a fresh splat of the flat, address-space-0 pointer and erasing the old, now-dead broadcast chain. A *nested* `getelementptr` (a groupshared array of arrays/structs, one level deeper than a single index) remains diagnosed, unchanged from before. `test/Transforms/CPU/simdize-groupshared-{divergent-index,atomic-array,masked-store-uniform,masked-store-gep,nested-gep-unsupported}.ll` cover the new shapes and the one still-diagnosed narrower case; existing `simdize-groupshared-{uniform,atomic-scalar}.ll`/`entry-wrapper-groupshared-{host,stack}.ll` coverage is unchanged and still passes) | prerequisite | §1.6, §1.8.1 | — |
| R24 | Barrier inside a surviving *branch*, and a `phi` live across a group-sync barrier (done: `feme::cpu::matchBranchShape` recognizes a uniform two-way branch -- guaranteed uniform, since a divergent branch a barrier could survive inside is already gone by `feme::cpu::LinearizePass` -- whose arms are each a linear chain reconverging at a merge block with no phi of its own; `feme::cpu::buildWrapperForBranch` clones the branch's own (pure, uniform-parameter-only) condition directly into the wrapper as an ordinary scalar `br`, run once for the whole group, and barrier-splits each arm exactly like a straight-line wave body (`feme::cpu::splitArmAtBarriers`), with the wrapper's own real control flow choosing which arm's wave loops run. `feme::cpu::spillValuesLiveAcrossBarriers` now also spills a `phi` live across a barrier exactly like any other value, placing its spill store after its own block's last phi rather than immediately after itself. Two narrower shapes remain diagnosed rather than mis-compiled: a branch merge block with a phi (a value one arm computes differently from the other, which would mean spilling across the wrapper's own scalar branch choice, not just across a barrier within one region) and a value live across a barrier *within* one arm (each arm is barrier-split independently, and `feme::cpu::EntryWrapperPass` allocates only one `barrier_spill` buffer per wrapper, which two independently split arms cannot safely share). `test/Transforms/CPU/entry-wrapper-barrier-{in-branch,live-phi-spill,in-branch-merge-phi-unsupported,in-branch-arm-spill-unsupported}.ll` cover the newly-supported shapes and the two still-diagnosed narrower cases; `EntryWrapperTest.{SplitsBarrierInsideUniformBranch,BranchMergePhiIsDiagnosed,SpillsPhiLiveAcrossGroupSyncBarrier}` cover the same at the IR-shape level) | prerequisite | §1.6, §1.8.1 | R23 |
| R25 | Root-constant breadth: any register-bound constant buffer rather than only `(b0, space0)`, array and non-constant-row-index shapes, and the full advertised push-constant range (done: `feme::cpu::RootConstantLoweringPass`'s `matchRootConstantHandle` now accepts any single, finite `(space, register)` `dx.CBuffer` binding rather than only `(b0, space0)` -- reported back via new `RootConstantAccess::Space`/`Register` fields, threaded into `!feme.cpu.resources`'/`StageArtifactInfo`'s own new `RootConstantSpace`/`RootConstantRegister` fields (`ArtifactAbiVersion` bumped to 4) so a host knows which binding a given `RootConstantSize` belongs to; two or more distinct bindings in the same function remain ambiguous (there is still only one root-constant block) and are left for `checkSupportedRaisedOps` to reject exactly as a single non-default binding was before this row. An array binding (`RangeSize > 1`) is accepted, keyed by the `handlefrombinding` call's own `Index` operand (constant or dynamic); a `cbufferrow.4` load's row index may likewise be constant or dynamic now (`RootConstantRowLoad::Row` generalized from a resolved `uint64_t` to a `Value*`, constant-folding back down to the exact code a compile-time row produced before whenever it happens to be one). `lowerRootConstantAccess` reports the binding's *full advertised* byte span (`ElementSize * RangeSize`, the declared `dx.CBuffer` handle type's own byte length times the array length) rather than only the rows a function's own loads happen to touch statically, which R12's approach could no longer even compute once a row or array index is dynamic. Deviation: fixing this exposed a latent ordering bug `matchRootConstantAccess`'s new `Value*` row/index made reachable for the first time -- `feme::cpu::lowerFunctionRootConstants` matched against the *original* function, then rebuilt it (`addRootConstantParams` moves the body to a new `Function`, RAUW'ing every argument), leaving any `Value*` captured from a stale `Argument` dangling; fixed by re-matching against the rebuilt function, mirroring what `feme::cpu::ResourceLoweringPass`'s own combined path already did. `--cpu-root-constants=bN,spaceM` remains unimplemented and is no longer needed for the reason it was proposed, since any single binding is now recognized automatically; an override only matters once more than one binding must be disambiguated, which is unrelated future work. `test/Transforms/CPU/root-constant-lowering.ll` covers a non-default binding, an array binding with a dynamic index, a dynamic row index, two-distinct-bindings rejection, and the original constant-row default-binding shape (re-verified byte-for-byte identical codegen); `UnsupportedOpsTest`/`ResourceLoweringTest`/`ResourceInfoTest` cover the same at the pass-interaction and metadata/ABI level) | prerequisite | §1.8.1 | — |
| R26 | A SPIR-V descriptor-set binding-to-heap normalization matching DXIL's `BoundResourceNormalizationPass`, with arrayed bindings in contiguous heap ranges and dynamic buffer offsets (done: `feme::cpu::SPIRVResourceLoweringPass`'s `collectHandles` now reads `llvm.spv.resource.handlefrombinding`'s own range-size and array-index operands instead of assuming an implicit range size of 1 -- the range size must still be a compile-time constant, exactly like the (set, binding) identity itself, but the array index may be dynamic, matching `feme::cpu::BoundResourceNormalizationPass`'s own DXIL treatment. `assignHeapBases` assigns each non-conflicting identity a contiguous *run* of `RangeSize` heap slots (not a single slot), and `lowerAccesses` range-checks and clamps the array index into that run with `computeClampedIndex`/`computeOverflowClampedIndex` -- copied from `BoundResourceNormalizationPass` rather than shared, matching this file's existing precedent of duplicating `addResourceEnvParams` for its own differently-shaped signature. An unbounded range (`RangeSize == 0`, SPIR-V's own spelling of an unbounded descriptor array) is left un-normalized, and two handles at the same (set, binding) identity disagreeing about the range size (not just the element stride) are now a conflicting declaration too, both matching the DXIL pass's own rejections. Deviation: the array index is deliberately *not* cached alongside the handle -- it is re-read from the handle's own operand at lowering time instead, avoiding the exact stale-`Argument`-pointer bug R25 fixed in `RootConstantLowering.cpp` for the same reason (`addResourceEnvParams` rebuilds the handle's function, RAUWing every argument and erasing the original, so a `Value*` captured beforehand from one of its arguments would dangle). A Vulkan *dynamic* storage/uniform buffer offset needs no shader-side model at all, and this row adds none: per "Memory and Buffers" in feme/docs/FeMeVulkanDesign.md, a dynamic offset is folded into `FemeDescriptor::Data` when a host materializes a dispatch's physical heap, exactly like every other buffer's binding offset -- the existing `BoundResourceRange`/`materializeResourceHeap` model (feme/include/feme/Target/CPU/ResourceInfo.h, ResourceHeap.h) already carries this without any Vulkan-specific reflection record, answering FeMeVulkanDesign.md's open questions 3 (a separate SPIR-V-specific pass, not a raised `SPV_EXT_descriptor_heap`) and 7 (yes, the existing metadata carries it). `test/Transforms/CPU/spirv-resource-lowering-array.ll` covers an arrayed, dynamically-indexed binding; `spirv-resource-lowering-unsupported.ll` gains an unbounded-array case and `spirv-resource-lowering-conflicting.ll` a range-size-only conflict; `unittests/Transforms/CPU/SPIRVResourceLoweringTest.cpp` covers the same at the pass level, mirroring `BoundResourceNormalizationTest.cpp`'s structure) | prerequisite (V2) | §1.2, §1.9 | — |
| R27 | `StageCompileOptions` and a stage-aware `runPipeline` (compute-only overload retained), stage-aware `PreparePass` and pre-mutation graphics validation, and the split of the single active mask into live and side-effect masks through `LinearizePass`/`SIMDizePass` and the reference path (done: `feme::cpu::StageCompileOptions` (`feme/include/feme/Target/CPU/Pipeline.h`) carries `Stage`/`EntryPoint`/`WaveSize`, and a new `runPipeline(llvm::Module &, const StageCompileOptions &)` overload selects the entry point by `feme::ShaderStage` instead of assuming compute, tagging its `PipelineResult` with the compiled `Stage`; the original `runPipeline(llvm::Module &, llvm::StringRef, unsigned)` is kept, unchanged in behavior, as the compute-only compatibility overload the design explicitly asks to retain, and every existing caller (`JITEngine`/`CompiledStage`, `Driver`) still goes through it. For a non-`Compute` stage, `runPipeline` also runs `feme::graphics::ValidateStagePass` against the incoming module before `feme::cpu::PreparePass` (already stage-selecting since R16) or any other pass mutates it -- the "pre-mutation graphics validation" gate -- diagnosing a `feme.stage.*` violation while the module is still in its as-authored shape. `feme::cpu::LinearizePass`'s `DiamondFlattener`/`LoopLinearizer` now thread a `MaskPair{Live, SideEffect}` instead of a single scalar mask: `feme.stage.discard(cond)` narrows both by `!cond`, `feme.stage.demote(cond)` narrows only `SideEffect`, and `feme.stage.is_helper()` lowers to `Live && !SideEffect`; a plain `load`/resource load uses `Live`, a `store`/`atomicrmw`/resource store uses `SideEffect` -- the two masks are identical for any function with neither call (every existing compute shader), so this is a strict extension verified by updating every existing Linearize/SIMDize lit test's CHECK lines to the mechanically-renamed IR shape with `check-feme` still 100% green. `feme::cpu::SIMDizePass` needed no code changes: it already widens whichever `i1` value governs a masked call generically. `--reference` mode gets its own counterpart in `feme::cpu::ReferenceLoweringPass`: one invocation at a time has no mask to narrow, so `discard` becomes a real conditional early return (splitting the block right after the call) and `demote`/`is_helper` read/write a per-invocation `helper` `alloca` instead. Deviations, recorded in FeMeGraphicsDesign.md's "CPU Lowering Pipeline"/"Preparation and validation"/"Shared middle-end phases" Status notes: the mask split and `--reference`'s early return only cover the divergent-diamond/divergent-loop-exit shapes `LinearizePass` already supported before this row (a stage-mask call inside an otherwise-uniform loop is diagnosed, not mis-widened); `--reference` mode does not yet suppress a `demote`d invocation's later side effects; `CompiledStage::create` is not yet stage-aware (still `JITOptions`-only, left to R28 alongside the vertex/fragment wrappers that would actually dispatch a non-compute artifact); and validation's remaining bullets (wave-size range, resource/image/sampler kinds, patch/mesh/ray layout limits, structured shared reflection) have no implementation to gate on yet. `unittests/Target/CPU/PipelineTest.cpp`, new `LinearizeTest`/`ReferenceLoweringTest` cases, and two new `test/Transforms/CPU/Linearize/*.ll`/one new `test/Transforms/CPU/reference-lowering-stage-mask-ops.ll` cover this row; `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G1 | §1.8.3 | R20, R21 |
| R28 | Vertex and fragment wrappers over in-memory synthetic stage layouts, `FemeStageLayout`/`FemeVertexArgs`/`FemeFragmentArgs`, and derivative/quad lowering at wave sizes 4 and 8 (done: `feme/include/feme/Target/CPU/RuntimeABI.h` now defines the stage-batch ABI -- `FemeShaderResources`, dense-by-`ElementID` `FemeStageLayout`, `FemeVertexInvocation`, `FemeFragmentInvocation`, `FemeFragmentResult`, `FemeVertexArgs`, `FemeFragmentArgs`, and `StageArgsAbiVersion`; `feme::cpu::PreparedVertexBatch`/`PreparedFragmentBatch` (`feme/include/feme/Target/CPU/ResourceHeap.h`) materialize those argument blocks with the same ownership/lifetime convention `PreparedDispatch` already used for compute. The CPU pipeline's final wrapper is now stage-selected: `runPipeline` keeps `EntryWrapperPass` for `Compute`, but `VertexWrapperPass` and `FragmentWrapperPass` lower `feme.stage.input.load`/`output.store` against those layouts and emit the same `feme_cpu_entry_<name>` ABI symbol for vertex/fragment stages. `feme::cpu::WaveLoweringPass` now lowers `feme.stage.derivative.{x,y}.{fine,coarse}` and `feme.stage.quad.read` at wave sizes 4 and 8 with the fixed `(0,0),(1,0),(0,1),(1,1)` quad mapping, diagnosing any other wave size that reaches one of those ops instead of mis-lowering it; pull-model interpolation (`InterpolateAt*`) remains diagnosed as unsupported, explicitly scoped out of this row. `feme::cpu::CompiledStage` is now stage-aware: a new `create(Context &, Module, const StageCompileOptions &)` overload, `getStage()`, `invokeVertices`, and `invokeFragments` JIT-compile and execute real vertex/fragment batches end to end, and `StageArtifactInfo` now reports their `Stage` plus serialized `EntrySignature` when one is attached. Decision point finding: the shared middle end did **not** need a boundary revision. The localized extensions actually required were confined to the existing shared phases themselves -- `WaveUniformity` now classifies stage IO/derivative/quad ops as per-lane, `LinearizePass` rewrites `feme.stage.output.store` to a masked internal helper and records fragment return masks, and `SIMDizePass`/`WaveBodyEnv` grow the second entry side-effect mask plus widening for the stage-op/internal-helper calls that R27 had left compute-only. `PreparePass`, `ResourceLoweringPass`, and the resource/root-constant lowering path needed no stage-specific changes. Coverage: new lit tests `wave-lowering-derivative-{quad,unsupported}.ll`, `vertex-wrapper-stage-io.ll`, and `fragment-wrapper-stage-io.ll`; new wrapper/unit tests and `CompiledStageTest` end-to-end vertex/fragment invocation cases all pass with `check-feme`) | G1 | §1.8.3 | R27, R22 |
| R29 | The image and sampler descriptors, `FemeShaderResources` folded into `FemeDispatchArgs`, and `SamplerHeap` retyped. This is the deliberate ABI break: artifacts built before it stop loading (done: `feme/include/feme/Target/CPU/RuntimeABI.h` now defines `FemeImageDescriptor` (base allocation/size, `ImageDimension`, extent, mip/array/plane/sample counts, format, a dense-by-mip-level `FemeImageSubresourceLayout` row/slice/sample-pitch table, and sampled/storage/depth flags) and `FemeSamplerDescriptor` (min/mag/mip filter, per-axis addressing modes, LOD bias/clamp, comparison function, border color, anisotropy, reduction mode); `FemeShaderResources` gained `ImageHeap`/`ImageHeapCount` and its `SamplerHeap` is now `const FemeSamplerDescriptor *`; `FemeDispatchArgs` no longer declares its own `ResourceHeap`/`SamplerHeap`/`RootConstants` fields, instead embedding a `FemeShaderResources Resources` member so compute, vertex, and fragment stages share exactly one resource-binding contract. `feme::cpu::EntryWrapperPass`/`ReferenceEntryWrapperPass` read the embedded block through a new `loadResourcesField` helper (`lib/Transforms/CPU/DispatchArgsLayout.h`), and `feme::cpu::PreparedDispatch`/`PreparedVertexBatch`/`PreparedFragmentBatch` (`feme/include/feme/Target/CPU/ResourceHeap.h`) grew matching image-heap/retyped-sampler-heap fields on their `*Resources` input structs. Canonical `feme.image.*`/`feme.sampler.*` operations, format conversion, and sampling/addressing math are scoped out to R30) | G2 | §1.8.4 | R22 |
| R30 | `feme.image.*`/`feme.sampler.*` canonicalization from DXIL (including §1.3's texture/sampler handle-kind gap) and SPIR-V (including §1.2's sampling variants), the `runtime/CPU` sampling helpers (1D/2D addressing, mip layout, point/linear filtering, explicit and implicit LOD, addressing modes, comparison sampling), the initial format table with sRGB, and active-lane SIMD lowering. **Completes G2**, unblocking V5 and W3 (status: 2D sampling/loading landed end to end -- DXIL `Sample`/`SampleLevel`/`TextureLoad`/`GetDimensions.x` raising, SPIR-V `ImageSampleExplicitLod` conversion, `runtime/CPU`'s addressing/filtering/mip-selection/comparison-sampling/format-table helpers, and `feme::cpu::ResourceLoweringPass`'s consumption of both into `feme.cpu.image.*`, wired through `EntryWrapperPass`/`ReferenceEntryWrapperPass`/`VertexWrapperPass`/`FragmentWrapperPass` -- but G2 is not yet *complete*: 1D/3D/cube sampling, bias/gradient sampling, comparison sampling on DXIL (blocked upstream -- no numbered DXIL wire opcode exists in this LLVM tree to raise from), gather, active-lane SIMD widening for a *divergent* sample (`feme::cpu::SIMDize.cpp`'s call-widening only recognizes `feme.cpu.resource.*`'s shape, not `feme.cpu.image.*`'s -- a uniform sample already works), and CPU-side lowering of a SPIR-V-sourced image/sampler heap all remain, each documented in FeMeGraphicsDesign.md's "Canonical image operations"/"Texture layout and formats" status notes) | G2 | §1.8.4, §1.2, §1.3 | R29 |
| R31 | `FeMeGraphics` skeleton: normalized pipeline and prepared-draw descriptions, the `feme-render` tool (already specified in Design.md's "Testing Tools" and `docs/CommandGuide/feme-render.md`, along with its scene and image fixture formats -- only the implementation is left), and the heap YAML image resource class (done: new `FeMeGraphics` library (`feme/include/feme/Graphics`, `feme/lib/Graphics`) defines `feme::graphics::GraphicsPipeline`/`PreparedDraw` as plain description types -- the former owns the compiled vertex/fragment `feme::cpu::CompiledStage`s plus primitive topology, raster/depth/blend state and attachment formats, the latter holds one draw's attachments, viewport/scissor, vertex buffers, resource heap and draw commands -- matching "Normalized pipeline" in FeMeGraphicsDesign.md, but implementing no clip/raster/interpolation logic (that is R32). The same library implements the textual image fixture (`feme::graphics::ImageFixture`) and scene (`feme::graphics::Scene`) formats "Textual scene and image fixtures" in Design.md specifies, shared by the new `feme-render` tool and `unittests/Graphics/` as that section requires; fixture format coverage matches what `runtime/CPU`'s image helpers already implement (`R8G8B8A8_*` and the `R32*_FLOAT/UINT/SINT` family) and grows mechanically on demand. `feme-render` (`feme/tools/feme-render`) implements the CLI docs/CommandGuide/feme-render.md already specified: it parses a scene, builds and clears every attachment, compiles `pipeline.vertex`/`pipeline.fragment` into a real `GraphicsPipeline` when a scene has one, and dumps attachments (`--dump`, `--expect`, `--tolerance`); a non-empty scene `draws` list is diagnosed as not implemented rather than silently misrendering, since R32 is what actually executes one. `feme-run`'s heap YAML gains an `images` list (`ImageEntry` in feme-run.cpp), building `feme::cpu::FemeImageDescriptor`s into the ABI's separate image heap alongside `resource-heap`/`bindings`, covering a single mip level and (for a non-array dimension) a single array layer; multisample dimensions are rejected, matching G4's later multisample milestone. Shader modules `feme-render` loads are plain already-raised LLVM IR only for now -- DXIL/SPIR-V import follows `feme-run`'s own precedent once a test needs it. `test/Tools/feme-run/heap-image*.ll`, `unittests/Graphics/{ImageFixture,Pipeline,PreparedDraw,Scene}Test.cpp`, and `test/Tools/feme-render/*.test` cover this row; `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G3 | §2.6.1 | R28 |
| R32 | Vertex/index fetch, triangle assembly, clipping, viewport transform, culling, tile binning, top-left coverage, interpolation, and both stages run through the executor: one color attachment, one viewport/scissor, no MSAA. **Completes G3** (done: `feme::graphics::executeDraws` (`feme/include/feme/Graphics/Executor.h`, `feme/lib/Graphics/Executor.cpp`) implements the "Draw flow" FeMeGraphicsDesign.md specifies end to end for one `TriangleList`/`TriangleStrip` draw against one color attachment: vertex/index fetch decodes bound vertex-buffer attributes (the 32-bit float/int family and `R8G8B8A8_*`) matched to a vertex-stage input by `Location`; vertex-output/fragment-input varyings are linked by `Location` (Vulkan-style, since no `StageInterfaceMap` exists yet); triangles are clipped against all 6 homogeneous half-spaces (plus a `w > 0` guard) via Sutherland-Hodgman and fan-triangulated; viewport transform, front-face culling and top-left-rule coverage share one self-consistent directed-edge-function convention; primitives are binned into fixed-size tiles, each batching its own covered 2x2 quads (with helper lanes) into one `invokeFragments` call, with output merge performed in submission order (painter's algorithm, since depth testing is R33's); interpolation is perspective-correct/no-perspective/flat per `SignatureInterpolationMode`. `PreparedDraw` (R31) grew `VertexBufferBinding::Attributes` and `IndexBufferBinding`/`DrawCommand::Indexed` for index buffers; the scene YAML (`feme::graphics::Scene`) grew a matching `index-buffer` key and per-draw `indexed`/`first-index`/`vertex-offset` fields. `feme-render` now executes a scene's `draws` instead of diagnosing them as unimplemented, encoding `vertex-buffers`/`index-buffer` scene data into the executor's byte layouts and defaulting `viewport`/`scissor` to the sole color attachment's extent when the scene omits them. Deferred, each a documented scope note in Executor.cpp's own file comment: no post-transform vertex cache (every vertex re-runs, matching "the first implementation may perform all vertex work before tile work"); matrices/16-/64-bit stage elements; a non-`Float` varying is carried from the first vertex of the *rasterized* (possibly clipped) triangle rather than tracking the original mesh's provoking vertex through clipping; and `--workers`/`--tile-order`/`--reference` remain accepted-but-inert in `feme-render` (the executor is already a deterministic single-threaded scalar implementation, so every value of each produces identical output, satisfying but not yet exercising the metamorphic checks "Determinism and Reference Execution" describes -- true parallel tiling and a differential scalar-reference path are scheduling optimizations, not part of this milestone's correctness scope). `unittests/Graphics/ExecutorTest.cpp` covers full/partial coverage, indexed draws, back-face culling, unsupported-topology rejection, perspective-correct interpolation, and the top-left tie-break's gap/overlap-free adjacent-triangle property; `test/Tools/feme-render/draw-{triangle,vertex-buffer,indexed}.test` cover the CLI path. `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G3 | §1.8.5 | R31, R30 |
| R33 | Depth/stencil attachments with legal early/late scheduling, blending, write masks, logic ops, multiple render targets, multisample coverage and resolves, the format expansion the first advertised profile needs, and deterministic parallel tiled schedules (done: `feme::graphics::executeDraws` implements every bullet against `feme::graphics::GraphicsPipeline`'s new `StencilState`/`BlendState`/`LogicOp`/`getColorBlends()` state and `PreparedDraw`'s new `DepthStencilAttachment`/`ResolveAttachments`. Depth (`D16_UNORM`/`D32_FLOAT`) and stencil (`S8_UINT`) are two separate attachments rather than one packed `D24_UNORM_S8_UINT`/`D32_FLOAT_S8X24_UINT` surface (those formats are declared in `cpu::ResourceFormat` for a future API frontend to translate into/out of, but not yet given clear/pack/unpack support -- a mechanical, on-demand addition matching this codebase's established "declared, not yet wired" convention). Early-vs-late depth/stencil scheduling is chosen from the fragment stage's own already-existing reflection (`SignatureSystemValue::Depth`/`StencilRef` outputs, `FEME_CPU_ARTIFACT_USES_DISCARD`/`_DEMOTE`) -- no new reflection pass was needed, since the compute track's discard/demote lowering and the graphics signature model already carried everything "Early and late tests" in FeMeGraphicsDesign.md needs. Blending implements the full Vulkan/Direct3D-shared `BlendFactor`/`BlendOp` equation plus a per-channel write mask honored regardless of blend/logic-op state; logic ops are implemented for `R8G8B8A8_UNORM/_UINT/_SINT` only, matching both APIs' own restriction on which formats support one. Multiple render targets link each fragment `SV_TargetN` output to `Draw.Attachments[N]` with its own `BlendState`. Multisampling supports 1/2/4 samples at fixed, deterministic per-pixel sample offsets (FeMe's own convention, not copied from either API's standard pattern, per "Determinism and Reference Execution"'s fixed-sample-location requirement); coverage is tested per sample (`FemeFragmentInvocation::Coverage`'s long-documented "coverage mask" meaning, previously always 0/1) and depth/stencil tested/written per sample against its own stored value, but shading and the depth/stencil candidate value stay per-pixel -- a documented precision scope decision, not a gap in the coverage/resolve correctness a completion test observes. Depth/stencil resolve and 8+ sample counts are mechanical follow-ups. Deterministic parallel tile scheduling dispatches `processTile` across `WorkerCount` worker threads pulling from a shared atomic cursor; since tiles own disjoint attachment regions, output is bit-identical regardless of worker count or tile order, wiring up `feme-render`'s previously-inert `--workers` flag. `feme::graphics::Scene`/`feme-render` grow `depth-attachment`/`stencil-attachment` scene keys (documented in Design.md); MRT/blend/stencil/MSAA scene YAML wiring beyond the depth attachment is not yet added to `feme-render` itself -- a mechanical follow-up, since the executor library is what this row's completion test exercises directly. `unittests/Graphics/ExecutorTest.cpp` covers depth test/write/early-late scheduling, stencil test/ops, blending, write masks, logic ops, MRT, multisample coverage/resolve, sample-count rejection, and the worker-count determinism property; `unittests/Graphics/ImageFixtureTest.cpp` covers the new depth/stencil fixture formats and `unpackColor`; `test/Tools/feme-render/draw-depth.test` covers the depth attachment end to end. `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G4 | §1.8.5, §2.6.3 | R32 |
| R34 | Geometry/hull/domain signatures and wrappers, patch storage, control-stage barriers, tessellator state and domain-coordinate generation, bounded geometry streams, stream output, adjacency, layered rendering (status: the host-side, standalone-tested core lands -- the signature/stage-op model (`SignatureSystemValue::TessFactorEdge`/`TessFactorInside`/`DomainLocation`/`OutputControlPointID`, `StageOpKind::StreamEmit`/`StreamCut`, patch input/output reusing the existing `InputLoad`/`OutputStore` ops), the fixed-function tessellator (`feme::graphics::tessellate`, new Tessellator.h, generating domain coordinates/connectivity for isoline/triangle/quad domains across every partitioning/output-primitive combination; triangle/quad interiors subdivide uniformly from the largest/inside factor rather than placing per-edge boundary vertices and stitching a crack-free fan, a documented scope note in its own file comment), bounded patch storage (`feme::graphics::PatchRecord`, new Patch.h -- control-stage barriers need no new code, since `feme::cpu`'s groupshared/barrier lowering is already stage-agnostic), the four adjacency `PrimitiveTopology` variants plus list- and strip-topology adjacency splitting (Pipeline.h's `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency`), a bounded per-invocation multi-stream geometry builder (`feme::graphics::GeometryStreamBuilder`, new GeometryStream.h) retaining strip boundaries/emission order for stream output and rasterization to share, and layered-rendering array-layer selection that discards rather than clamps an out-of-range index (`feme::graphics::resolveRenderTargetArrayLayer`, new LayeredRendering.h, plus `AttachmentView::ArrayLayers`). Deferred, each documented in its own file's comment: compiling a real hull/domain/geometry entry point through the CPU lowering pipeline into an invokable `CompiledStage` batch (neither stage has a `VertexWrapperPass`/`FragmentWrapperPass` counterpart yet) and wiring the result into `executeDraws`/`feme-render`; SIMD-lane stream-range reservation via checked prefix sums; and crack-free non-uniform per-edge tessellation. `unittests/Graphics/{Tessellator,Patch,GeometryStream,LayeredRendering}Test.cpp` and `PipelineTest.cpp`'s/`SignatureTest.cpp`'s/`StageOpsTest.cpp`'s new cases cover today's scope; `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after -- G5 is not yet complete, since no image-comparison completion test exists) | G5 | §1.8.5 | R33, R24 |
| R35 | Amplification and mesh stages: import and canonicalization, bounded payload and mesh-output builders reusing the compute workgroup/barrier/wave lowering, checked dispatch queues, meshlet assembly and validation, feeding the shared clip/raster path | G6 | §1.8.5 | R33, R24 |
| R36 | `FeMeRayTracing`: canonical acceleration structures and deterministic builders, triangle/instance traversal with bounds validation, inline ray-query import from DXIL and SPIR-V, ray/payload/attribute/callable/record/continuation reflection, scalar traversal from compute and fragment shaders | G7 | §1.8.5 | R30 |
| R37 | Ray-tracing pipelines: all six stage kinds plus hit-group linkage, the continuation transform, entry/resume wrappers, bounded frame allocation, shader-record translation, recursion enforcement, and packetization only once scalar continuation execution is the differential reference | G8 | §1.8.5 | R36, R28 |

Three ordering notes that are not visible in the dependency column:

- **R21 and R22 come first for the runtimes, not for graphics.** They are the
  only steps Vulkan V1 and Direct3D W1 need from this table, so landing them
  early lets both runtime tracks start while R16–R20 are still in progress.
- **R29's ABI break is cheap exactly once.** It must land before any artifact
  format is depended upon outside this tree, which in practice means before
  V4's persistent pipeline cache and W5's validated cache.
- **R23/R24/R25 have no graphics dependency of their own** and close §1.6
  narrowings that are already wrong answers waiting to happen. They can land
  at any time and unblock the most milestones per unit of work.

### 3.3 API runtime tracks

These milestones live in FeMeVulkanDesign.md and FeMeWARPDesign.md; they are
reproduced here only with their dependencies on §3.2, since neither document
can see this tree's schedule. Both tracks may run concurrently with each other
and with §3.2 once their prerequisites land.

| # | Milestone | Depends on |
|---|---|---|
| V0 | Loader-visible skeleton: optional Vulkan-Headers dependency, `vk.xml` entrypoint generator, hidden-visibility ICD with a version script and development manifest, instance/physical device/device/compute queue, truthful properties and limits, loader smoke and two-ICD coexistence tests | — (new build-system work, §1.9) |
| V0.5 | SPIR-V import that survives real shaders: a glslang/DXC/Clang corpus, the decision between fixing MLIR's structurized deserializer and translating the SPIR-V CFG to unstructured LLVM IR for `PreparePass` to restructure, a prototype of the chosen approach, and the importer fuzzer extended to it | — (may change V1's design; schedule before V1) |
| V1 | Empty compute dispatch: memory, buffers, shader modules, pipeline layouts, command pools/buffers, group-size resolution, submit/fences/idle, direct, base and indirect dispatch | V0, V0.5, R21, R22 |
| V2 | Storage buffers and descriptors, descriptor pools/sets/updates and dynamic offsets, buffer copies and barriers, lavapipe differential | V1, R26, R23 |
| V3 | Push constants onto FeMe root constants, uniform buffers, binary and timeline semaphores, secondary command buffers, events, query pools | V2, R25 |
| V4 | Typed buffers, `VkFormat` mapping, texel buffers, broader subgroup/atomic/robustness coverage, persistent pipeline cache with a blob fuzzer, first CTS runs over the advertised subset | V3, R22 |
| V5 | Images and sampling: image memory requirements, views, layout tracking, copies, storage and sampled images, samplers | V4, R30 |
| V6 | Graphics queue and basic rendering: graphics stage compilation, `VkRenderPass` and dynamic rendering, graphics pipeline state, draws, and `VK_QUEUE_GRAPHICS_BIT` | V5, R32, R33 |
| V7 | Tessellation, geometry, and graphics completeness: the optional stages, queries over real draws, layered rendering, multi-viewport, and the advertised format matrix | V6, R34 |
| V8 | Mesh shading, ray tracing, and presentation: `VK_EXT_mesh_shader`, the ray-tracing extension set with validated build inputs and shader binding tables, and WSI starting with `VK_EXT_headless_surface` | V7, R35–R37 |

| # | Milestone | Depends on |
|---|---|---|
| W0 | Integration feasibility prototype: SDK/WDK/DDI/OS version selection, proof that a software adapter can be installed and enumerated through DXGI or selection of the application-local compatibility-runtime boundary instead, and the signing/INF/CI/debugging story. Throwaway-capable, and gates every other W step | — |
| W1 | Headless compute device: objects, DXIL import through the existing path, a resource-free dispatch, multi-wave barrier correctness, device-removal propagation | W0, R21, R22 |
| W2 | Root signatures of both versions, descriptor heaps and tables, root constants and CBVs, raw/structured/typed buffers with UAV writes and atomics, dispatch/copy/barrier/query, WARP differential | W1, R25, R23 |
| W3 | Textures and sampling: image/sampler descriptor ABI, committed and placed layouts, copy footprints, views, the initial format matrix, UAV texture access | W2, R30 |
| W4 | Basic graphics: input assembly through pixel shading with derivatives, interpolation modes, helper lanes, one render target, depth, minimal blend | W3, R32 |
| W5 | Graphics completeness: depth/stencil, blending, MSAA, formats, MRT, queries, predication, stream output, indirect draw, tessellation and geometry, pipeline libraries and a validated cache, Windows conformance and HLK | W4, R33, R34 |
| W6 | Interoperability: DXGI presentation or cross-adapter prototype, shared resources and fences, D3D11-on-12 evaluation, mesh-shader and ray-tracing evaluation | W5, R35–R37 |

The capability rule from FeMeGraphicsDesign.md applies to both tables and is
the one scheduling constraint that cannot be traded away: neither runtime may
advertise a graphics-capable queue, `VK_QUEUE_GRAPHICS_BIT`, or a
raster-implying feature level until the corresponding G milestone's completion
test passes for *every* format and state combination it reports.

### 3.4 Documentation debt

Three documentation items were prerequisites for the steps above rather than
follow-ups, and each was small. All three are now written; each entry records
what was decided, since the decisions are what the later steps build on:

- **FeMeVulkanDesign.md has no V6–V8** (done). The graphics design listed
  what they unblock but explicitly did not own their Vulkan-side content
  (graphics queue family, `VkRenderPass`/dynamic rendering, graphics pipeline
  state, WSI). FeMeVulkanDesign.md's "Graphics, Presentation, and
  Window-System Integration" section now specifies all four — graphics adds
  `VK_QUEUE_GRAPHICS_BIT` to the *existing* universal queue family rather than
  inventing a second one, `VkRenderPass` and dynamic rendering are both
  implemented and normalized into one internal render-target binding, pipeline
  state translates into `FeMeGraphics`' normalized pipeline description with
  dynamic state resolved per draw, and WSI starts with
  `VK_EXT_headless_surface` before exactly one CI-exercisable platform
  surface — and milestones V6 (G3/G4), V7 (G5) and V8 (G6–G8 plus WSI) are
  written against it. R32's work now has a Vulkan consumer to aim at.
- **Design.md's tool list and `docs/CommandGuide/` need `feme-render`**
  (R31), which is also where the textual scene and image fixture formats
  should be specified (done). Design.md's "Testing Tools" now carries the
  tool, `docs/CommandGuide/feme-render.md` documents it, and Design.md's
  "Textual scene and image fixtures" specifies both formats — placed under
  "Avoiding binary test fixtures" rather than in the command guide, since the
  graphics unit tests and both API runtime suites consume the same formats.
- **DXIL texture/sampler handle kinds needed a decision recorded in
  Design.md's DXIL section** before R30 implements them — §1.3 had flagged
  this as blocking since before the graphics design existed (done).
  Design.md's "Decision: texture and sampler handle kinds" records it, and
  corrects the premise: the bits §1.3 said `ResourceProperties` does not
  carry are carried, so the decision is to decode them into LLVM's existing
  `dx.Texture`/`dx.MSTexture`/`dx.FeedbackTexture`/`dx.Sampler` types, with
  the legacy `!dx.resources` path recovering the texel width from access
  sites and normalized (UNORM/SNORM/packed) element kinds deferred to G2's
  format table.

## Part 4: Explicitly not scheduled

- MLIR `gpu`-dialect retargeting (Design.md Non-Goals — no client).
- DXIL → DXBC and SPIR-V → DXBC (Translation Matrix: "not a priority").
- HLSL/GLSL source front ends (Design.md Non-Goals).
- Standalone out-of-tree builds against an installed LLVM+MLIR (Design.md
  Goals: out of scope for now, no redesign needed to add later).
- Work graphs (FeMeGraphicsDesign.md, G8's closing note): a persistent,
  dynamically composed node graph with its own backing-memory model is not
  equivalent to amplification fanout or bounded ray continuation queues, and
  needs its own design.
- A native Direct3D 11 DDI (FeMeWARPDesign.md W6): evaluate D3D11-on-12
  coverage first.
- Vulkan video, sparse resources, and any extension outside the advertised
  manifest (FeMeVulkanDesign.md Initial Non-Goals).
- Transparent substitution for Microsoft's WARP binary or
  `D3D_DRIVER_TYPE_WARP`/`EnumWarpAdapter` interception (FeMeWARPDesign.md
  Status): those are Windows-owned selection paths, and a separately
  installed, explicitly selected adapter is the first deployment target.
