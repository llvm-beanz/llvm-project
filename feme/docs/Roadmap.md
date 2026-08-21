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
- **P1 — texture/sampler handle kinds (legacy path done by this roadmap
  step).** The blocking decision is now recorded in Design.md's DXIL section
  ("Decision: texture and sampler handle kinds"): the dimension/multi-sample/
  feedback bits this entry said were missing are in fact carried by
  `ResourceProperties` (Word0's `ResourceKind` byte *is* the dimension; Word1
  carries component type/count, sample count and feedback kind), so the
  raised handle types are LLVM's own
  `dx.Texture`/`dx.MSTexture`/`dx.FeedbackTexture`/`dx.Sampler`. What is
  actually left is implementation, plus two narrower gaps the decision names:
  the legacy `!dx.resources` path has no component count and must recover the
  texel width from access sites the way typed buffers already do, and
  UNORM/SNORM/packed element kinds stay unraised until G2's format table
  exists. Implemented by R30 for the bindless `handlefromheap`/
  `handlefrombinding` path (see Design.md's status note); the legacy
  `!dx.resources`-based path's texture kinds (`buildHandleType` generalized
  the same way `TypedBuffer` already worked, `inferTypedBufferWidth`
  generalized to read `dx.op.textureLoad`/`textureStore` too) are now raised
  as well -- `dxil-raise-legacy-resources.ll`'s `load_texture2d` covers this,
  closing the gap a real `dxc -T cs_6_2` (below SM6.6's dynamic-resource
  threshold, so it takes this legacy path) compute shader hit. `Sampler`
  (no access op raises to consume it yet either way) and UNORM/SNORM/packed
  formats remain future work.
- **P1 — the remaining resource access ops (done for texture load/store and
  the 16-/64-bit `CBufferLoadLegacy` row overloads by this roadmap step).**
  `raiseTextureStore` (DXIL opcode 67) is now symmetric with the existing
  `raiseTextureLoad`, using a new canonical `llvm.dx.resource.store.texture`
  intrinsic added upstream (DXIL's `TextureStore` op had a `DXILOpClass` but
  no numbered `DXILOp<67, ...>` definition, canonical intrinsic, or
  `DXILOpLowering` support at all, unlike `TextureLoad`/op 66); generalizing
  `raiseCBufferLoadLegacy` to `getCBufferRowIntrinsic`'s 2-/8-field 64-/16-bit
  row shapes (alongside the existing 4-field 32-bit one) covers a `cbuffer`
  of `double`/`half`/16-bit-int members, which `-enable-16bit-types`
  produces. Both were needed by the same real shader
  (`feme-dxil-to-amdgpu-texture.ll`): raw/structured buffer load/store and
  samplers remain the rest of this entry's scope. `GetDimensions`' `.xy`
  (width+height) field pair now raises too (`raiseGetDimensions`, plus a new
  `DXILOpLowering::lowerGetDimensionsXY` forward lowering added upstream the
  same way `TextureStore`'s was, and `feme::amdgpu::ResourceLoweringPass`'s
  new `Binding::NumDimensionArgs` kernel arguments to consume it when
  retargeting to `amdgcn-*` -- see `feme-dxil-to-amdgpu-texture-
  getdimensions.ll`); a mip-count out-parameter (`.z`/`.w`) is still left
  unraised, since there is no `levels_xy`-shaped forward lowering to verify
  against yet.
- **P1 — shader model 6.9's unified `FDot` op (done by this roadmap step).**
  A real `dxc -T cs_6_9` shader calling HLSL's `dot()` on a 16-bit vector
  (e.g. `dot(half2, half2)`) emits DXIL opcode 311 (`dx.op.dot.*`), not the
  older arity-specific `Dot2`/`Dot3`/`Dot4` (opcodes 54-56) this pass already
  covered -- `feme::dxil::OpRaisingPass` had no row for it at all, so
  retargeting such a shader to `amdgcn-*` failed with "'dx.op.dot.v2f32' is
  not supported when targeting 'amdgcn-amd-amdhsa'". `FDot` still fits
  the same table-driven `raiseCall` path as `Dot2`..`Dot4` (its overload key
  is still the first operand's type), it just takes its two operand
  *vectors* directly rather than 2*N separate scalars, matching
  `int_dx_fdot`'s existing signature; `feme::dxil::IntrinsicExpansionPass`
  gained a matching `expandFDot` (per-lane `extractelement` +
  `llvm.fmuladd` chain, mirroring `expandDot`'s scalar-operand version) so
  every non-DXIL target still sees plain LLVM IR. LLVM's own DirectX backend
  never emits opcode 311 itself (`DXILIntrinsicExpansion` scalarizes
  `llvm.dx.fdot` before `DXILOpLowering` runs), so this has no
  `-dxil-op-lower` round-trip test -- `feme-dxil-to-amdgpu-dot.ll` instead
  feeds a hand-written `dx.op.dot` call through the raw-bitcode import path
  (`llvm-as`, no `llc`) straight into the full `feme` CLI targeting
  `amdgcn-amd-amdhsa`.

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
- **P1 — `feme::amdgpu::RaisedLoweringPass`/`ResourceLoweringPass` breadth**
  tracks §1.2/§1.3: every newly-raised intrinsic needs an AMDGPU lowering or
  it becomes a new end-to-end failure on a path that used to work.
  `ResourceLoweringPass` now also models `dx.Texture` (a flat data pointer
  plus one trailing per-extra-dimension row/slice-pitch stride argument,
  `Binding::NumAuxArgs` -- DXIL carries no such stride itself, unlike a
  typed buffer's flat element index) and `dx.CBuffer` (a 16-byte-strided
  flat load, needing no extra argument) alongside its existing
  `dx.TypedBuffer`/`spirv.Image` support, closing the two-part gap §1.3's
  own entry above added raising for; doing so surfaced (and fixed) two
  `collectBindings` bugs only reachable once more than one DX resource
  family was modeled at once (mismatching a handle's family by intrinsic ID
  alone, and keying a binding by (space, register) alone despite HLSL's
  `t`/`u` registers being independent namespaces) -- see Design.md's status
  note on this section for both. `feme-dxil-to-amdgpu-texture.ll` is a new
  end-to-end test proving a real `Texture2D`/`RWTexture2D`/`cbuffer<half>`
  shader now retargets to AMDGPU; raw/structured buffers and samplers
  remain unmodeled.
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
| Vector/aggregate leaf decomposition narrower than the design (narrowed further by C3: `phi`, scalar-condition `select`, `shufflevector`, and non-constant-index `extractelement` producers/consumers are now supported alongside R12's resource-load producer and constant-index `extractelement` consumer; only a per-lane `<N x i1>`-condition `select` and every divergent aggregate remain diagnosed -- see FeMeCPUDesign.md's deviation note) | 7 | P2 |
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
| ~~No `feme.image.*`/`feme.sampler.*` operations and no sampling, filtering, mip-selection, addressing-mode, sRGB or format-conversion helpers in `runtime/CPU`~~ (closed by R30 for 2D images: DXIL's `dx.op.sample`/`sampleLevel`/`textureLoad` raise to LLVM's own `llvm.dx.resource.sample`/`samplelevel`/`load.level` -- the canonical, already target-generic-in-spelling intrinsics, not a bespoke `feme.image.*` dialect -- and `feme::cpu::ResourceLoweringPass` lowers a 2D `dx.Texture`/`dx.Sampler` pair's access into new `feme.cpu.image.sample.2d.v4f32`/`samplecmp.2d.f32`/`load.2d.v4f32` calls (`feme::cpu::ImageCalls`), implemented by `runtime/CPU/FeMeRuntimeCPU.c`'s point/bilinear filtering, all five addressing modes, explicit-LOD mip selection, PCF-style comparison sampling and an initial `R32G32B32A32_FLOAT`/`R8G8B8A8_UNORM(_SRGB)` format table with sRGB decode. A follow-up pass added the SPIR-V half (`feme::cpu::SPIRVResourceLoweringPass` lowering a bound 2D `spirv.Image`/`spirv.Sampler` pair, plus the image/sampler heap classes and materialization a host needs to feed it) and active-lane SIMD widening for a *divergent* sample; 1D/3D/cube sampling, arrayed/multisampled/storage images, bias/gradient sampling, gather and nonzero texel offsets remain -- see "Canonical image operations"/"Texture layout and formats" in FeMeGraphicsDesign.md for the itemized list and why each is deferred) | "Canonical image operations", "Texture layout and formats" | P0 |
| ~~DXIL texture/sampler handle kinds are unraised (§1.3's own P1 row; the encoding is now decided in Design.md's "Decision: texture and sampler handle kinds", so only the implementation is left) and SPIR-V sampling beyond basic `ImageSampleImplicitLod`/`OpImageFetch` is unconverted (§1.2's R9 entry)~~ (closed by R30 for the bindless heap path and the DXIL ops LLVM's own backend already lowers a canonical intrinsic to (`Sample`/`SampleLevel`/`TextureLoad`/`GetDimensions.x`/`.xy`), plus SPIR-V's `ImageSampleExplicitLod`; comparison sampling/gather have no numbered DXIL wire opcode in this LLVM tree to raise from at all, an upstream gap, not FeMe's -- see Design.md's "Decision: texture and sampler handle kinds" status note) | §1.2, §1.3 | P0 |

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

Owned by [FeMeVulkanDesign.md](FeMeVulkanDesign.md). V0 (see §3.3) landed
`lib/Vulkan`, the development manifest, and the `vk.xml` generator; the rows
below are what remains for V0.5 onward.

| Gap | Owner section | Priority |
|---|---|---|
| ~~The Vulkan SDK would be FeMe's **first external dependency**. FeMe is built in-tree only, with no optional external package pattern to copy: the configuration surface, the disabled-path CI coverage, and the `vk.xml` version floor are new project-wide obligations~~ (closed by V0: `feme/CMakeLists.txt`'s `FEME_ENABLE_VULKAN` option `find_package(Vulkan 1.3)`s using CMake's own FindVulkan module, leaving every other feme component buildable when it isn't found) | "Project and Library Boundaries" | P0 |
| ~~No generated entrypoint table; hand-maintaining command names, aliases, core-version promotions and extension guards is the failure mode the design rejects~~ (closed by V0: `feme/utils/vk_gen_entrypoints.py` reads vk.xml's core `VK_VERSION_1_0`/`VK_VERSION_1_1` `<feature>` blocks directly, resolving aliases and classifying each command's loader dispatch level; extension guards remain future work once an extension is implemented) | "Loader Integration" | P0 |
| Symbol visibility and LLVM coexistence: the loader loads *every* ICD into the process, so `libfeme_vulkan.so` shares an address space with Mesa drivers linking their own LLVM. Static LLVM/MLIR, `-fvisibility=hidden`, an exports version script, no `llvm::cl` registration on any reachable path, and one-shot target initialization under `std::once_flag` are hard requirements, verified by a two-ICD test and an exported-symbol-set link check (V0 closed everything but the last clause: `feme_vulkan` links LLVM/MLIR statically with `-fvisibility=hidden` and `libfeme_vulkan.map`, verified by `test/Vulkan/two-icd-coexistence.test` against Mesa lavapipe and `nm -D`'s exact four-symbol check; one-shot target initialization has nothing to guard yet, since V0 does no shader compilation -- see FeMeVulkanDesign.md's V0 Status note -- and is deferred to whichever milestone first JIT-compiles a pipeline) | "Process Coexistence and Symbol Visibility" | P0 |
| ~~**`feme::SPIRVImporter` cannot ingest realistic Vulkan SPIR-V at all.** It wraps `mlir::spirv::deserialize`, whose structurized reconstruction rejects an `OpPhi` in a loop merge block — which any loop carrying a value-producing `break` emits — and has been observed to fail on `OpCopyObject`. Only trivial control flow imports today~~ (closed by V0.5: `ImportOptions::SPIRVEnableControlFlowStructurization` now defaults to `false`, so `SPIRVImporter` deserializes straight to unstructured block arguments/branches unconditionally rather than attempting -- and only falling back away from -- structured reconstruction; the prototype found a *second* reason this had to be the default rather than a failure-triggered fallback, not only the documented `OpPhi` rejection: MLIR's own `spirv.mlir.loop` -> `llvm` dialect conversion pattern crashes on a loop-carried value even when structurization itself succeeds. Validated against a real `dxc -spirv` corpus (`feme/test/Tools/feme-run/SPIRV`), not only hand-written fixtures, and `feme-spirv-import-fuzzer`'s seed corpus gained a matching unstructured, multi-block seed. See FeMeVulkanDesign.md's "SPIR-V import prerequisites" Status note for the full writeup, including the still-open `OpCopyObject`/glslang-corpus gaps) | "SPIR-V import prerequisites" | P0 |
| ~~No SPIR-V binding-to-heap normalization: `feme::cpu::BoundResourceNormalizationPass` rewrites DXIL's `handlefrombinding` only, and R10's `SPIRVResourceLoweringPass` normalizes a *single* bound storage buffer directly, with no descriptor-set, arrayed-binding or dynamic-offset model~~ (closed by R26: `feme::cpu::SPIRVResourceLoweringPass` now reads `llvm.spv.resource.handlefrombinding`'s own range-size and array-index operands rather than assuming an implicit range size of 1, assigning each (descriptor set, binding) identity a contiguous run of heap slots and range-checking a (possibly dynamic) array index into it exactly as `BoundResourceNormalizationPass` does for a DXIL array binding -- see that pass's updated header comment and feme/docs/FeMeCPUDesign.md's "Bound-resource normalization". A Vulkan *dynamic* storage/uniform buffer offset needs no shader-side model at all: per "Memory and Buffers" in feme/docs/FeMeVulkanDesign.md, it is folded into `FemeDescriptor::Data` when a host materializes a dispatch's heap, the same way every other buffer's binding offset is, so the existing `BoundResourceRange`/`materializeResourceHeap` model already carries it without a Vulkan-specific reflection record -- answering FeMeVulkanDesign.md's open questions 3 and 7) | "Required SPIR-V resource work"; §1.2 | P0 |
| ~~Everything else in the object model — instance/device/queue,~~ memory, buffers, ~~command pools and buffers, submission, fences~~ (closed by V1's `lib/Vulkan/{Memory,Buffer,CommandBuffer,Sync}.{h,cpp}`), ~~descriptor pools/sets/updates~~ (closed by V2's `lib/Vulkan/Descriptor.{h,cpp}`), ~~binary and timeline semaphores, events, query pools~~ (closed by V3's `lib/Vulkan/{Sync,QueryPool}.{h,cpp}`), ~~pipeline cache~~ (closed by V4's `lib/Vulkan/PipelineCache.{h,cpp}`, with the "no relocatable object code yet" deviation FeMeVulkanDesign.md's "Pipeline Cache" section's own Status note records) | V0–V4 | P1 |
| ~~Images, image views, layout tracking, copies, storage/sampled images and samplers~~ (closed by V5's `lib/Vulkan/Image.{h,cpp}` plus the `Descriptor.{h,cpp}`/`CommandBuffer.{h,cpp}` extensions it needed, and shader-side consumption closed by R30's follow-up -- a bound sampled image and sampler now reach a real dispatch through the image/sampler heaps; see FeMeVulkanDesign.md's "V5" Status note) | V5 | P1 |
| Graphics, WSI and presentation: FeMeVulkanDesign.md's V6–V8 (done: its "Graphics, Presentation, and Window-System Integration" section now specifies the graphics queue family, `VkRenderPass`/dynamic rendering normalized into one render-target binding, graphics pipeline state translation, draw commands, the headless-first WSI decision, and mesh/ray exposure, and V6–V8 are written against G3–G8) | "Graphics, Presentation, and Window-System Integration" (Vulkan) | P1 |
| ~~A real Vulkan-CTS run had never actually happened (every milestone through V6 recorded only the *infrastructure* to run one)~~ (closed, once a VK-GL-CTS checkout became available: see feme/docs/VulkanCTSReport.md. Found and fixed four core commands this ICD had never implemented at all -- each crashed the loader's device dispatch table rather than merely rejecting -- `vkTrimCommandPool`, `vkCreateRenderPass2`'s command family, `vkCreateDescriptorUpdateTemplate`'s command family, and `vkCmdSetLineWidth`/`DepthBias`/`DepthBounds`/`DeviceMask`) | "V4"/"V6" Status notes; VulkanCTSReport.md | P0 |
| ~~An upstream MLIR SPIR-V deserializer bug (`processSpecConstantComposite`, `mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`) crashes on any spec-constant composite whose constituents are not themselves spec constants~~ (closed: `spirv.SpecConstantComposite`'s constituents now accept either a spec-constant symbol reference or an inline typed-attribute constant, and the six groups it crashed all run to completion) | VulkanCTSReport.md | P1 |
| An upstream MLIR `spirv`→`llvm` bug: `IComparePattern`/`FComparePattern` built their `llvm.icmp`/`llvm.fcmp` from the *unconverted* operands, so any comparison over a deserialized `si32` produced ill-typed IR (closed; it was 31% of all CTS failures by diagnostic count, though closing it moved rather than removed those failures -- see VulkanCTSReport.md, "Shader compilation") | VulkanCTSReport.md | P1 |
| **27,094 `dEQP-VK` cases fail, in 26 of 54 groups.** No case produces a wrong answer -- every failure is a clean rejection -- but the failure count is the gating number for any conformance claim, and it decomposes into four buckets with very different owners (78.3% shader compilation, 12.4% pipeline state, 7.2% format table, 2.1% API object model) | §1.9.1; VulkanCTSReport.md | P0 |

The SPIR-V import row is the largest single unknown in the Vulkan design, and
it is scheduled as its own milestone (V0.5) *before* V1 precisely because its
outcome — fixing MLIR's deserializer upstream versus translating the SPIR-V
CFG directly to unstructured LLVM IR and leaning on `feme::cpu::PreparePass`'s
existing structurizer — may change V1's design. It is also the one row here
that is a *FeMe* gap rather than a runtime gap, so it stays owned by this
roadmap even though the Vulkan document schedules it.

V0.5 settled that decision in favor of the unstructured path: see
FeMeVulkanDesign.md's "SPIR-V import prerequisites" Status note for the full
writeup, including a second, independent reason (a downstream MLIR
conversion crash on any loop-carried value, not only the originally
documented `OpPhi`-in-loop-merge-block deserialization rejection) that ruled
out "fix the upstream structurizer" as sufficient on its own even before
weighing cost.

#### 1.9.1 The road to Vulkan conformance

[VulkanCTSReport.md](VulkanCTSReport.md) measures where this ICD stands;
this section is the plan that measurement implies. Conformance is not the
same as "no failures": a Vulkan 1.2 conformance submission requires that
the mandatory CTS list run with **zero** `Fail`, *and* that the device
expose every mandatory feature, limit, format and queue capability, *and*
that a stock (unpatched) `deqp-vk` be used. The current run satisfies none
of the three, and the three are not independent -- most mandatory-capability
gaps are themselves the reason a test fails rather than reports
`NotSupported`.

Two framing decisions come first, because they change what the rest of this
list costs:

- **Conformance target version.** This ICD advertises apiVersion 1.2, which
  drags in every extension promoted through 1.2 as mandatory
  (`imagelessFramebuffer`, `uniformBufferStandardLayout`,
  `separateDepthStencilLayouts`, `hostQueryReset`,
  `shaderSubgroupExtendedTypes`, `subgroupBroadcastDynamicId`, `multiview`,
  timeline semaphores at their required limits -- all of which
  `dEQP-VK.info.device_mandatory_features` currently reports missing).
  Dropping the advertised version to 1.0 or 1.1 removes most of that set
  from the critical path at the cost of the claim. **This decision should
  be made before C1 below**, since it determines whether C6 exists at all.
- **Roadmap deviation to record.** FeMeVulkanDesign.md's V4 and V6 both
  frame the CTS as validation of an *intentionally narrow* advertised
  surface. Pursuing conformance inverts that: the surface must grow to the
  mandatory floor, and "reject cleanly" stops being a sufficient answer
  for anything the spec calls mandatory. That inversion is recorded here
  rather than silently applied; FeMeVulkanDesign.md's own scope sections
  stay authoritative for everything *above* the mandatory floor.

The ordered plan. "Cases" is the number of currently-failing `dEQP-VK`
cases the step is the *first* blocker for; because these blockers stack on
the same shaders (see VulkanCTSReport.md's note on the comparison-pattern
fix moving rather than removing 8,369 failures), the column is an upper
bound on each step's standalone value and a lower bound on its value once
its successors land. They do not sum to 27,094.

| # | Step | Cases | Owner | Priority |
|---|---|---:|---|---|
| C1 | ~~**Mandatory formats.** Add `B8G8R8A8_UNORM` (and the rest of the Vulkan mandatory color-attachment/blend table) to `isSupportedColorAttachmentFormat`, and at least one combined depth+stencil format (`D24_UNORM_S8_UINT` or `D32_SFLOAT_S8_UINT`) to `isSupportedDepthAttachmentFormat`/`isSupportedStencilAttachmentFormat`, backing each with a real pack/unpack path in `feme::graphics`. This is the cheapest step by far and unblocks every Amber-based CTS test, which is most of the CTS's own end-to-end coverage~~ (done: `isSupportedColorAttachmentFormat` now accepts `B8G8R8A8_UNORM` and `R10G10B10A2_UNORM` alongside the pre-existing `R8G8B8A8_UNORM`/`R16G16B16A16_SFLOAT`, completing the Vulkan 1.2 mandatory `COLOR_ATTACHMENT_BIT`/`_BLEND_BIT` set; `D24_UNORM_S8_UINT` is now a real combined depth+stencil format via `feme::graphics::packDepthClear`/`packStencilClear` and their `unpack*` inverses, each an independent read-modify-write of its own half of the shared 4-byte word so testing/writing one never corrupts the other; `RenderPass.cpp`'s `buildRenderTargetBinding`, `CommandBuffer.cpp`'s clears, `ImageOps.cpp`'s `vkCmdClearDepthStencilImage`/`vkCmdClearAttachments`, and `Executor.cpp`'s `readDepth`/`writeDepth`/`readStencil`/`writeStencil` all updated to match; see `FeMeVulkanDesign.md`'s "Deviations" note and `FeMeGraphicsDesign.md`'s R33 status note for the updated scope. Whether this actually moves CTS's 1,938-case count is unmeasured until C10 lands continuous measurement -- **measured, and it does not yet**: see VulkanCTSReport.md's "Roadmap C1: measured impact", which traces the remaining blocker to `vkGetPhysicalDeviceFormatProperties` unconditionally reporting zero support for every format (a separate, pre-existing stub CTS's own capability probes consult ahead of `vkCreateRenderPass`/`vkCreateGraphicsPipelines`) and records three newly-uncovered crashes a prototype fix for that stub surfaced, none in the format tables themselves) | 1,938 | `feme/lib/Vulkan/{RenderPass,Format}.cpp`, `feme::graphics` | P0 |
| C2 | ~~**`Uniform`-storage-class blocks.** Teach `feme::spirv::getBufferBlockElementArray`/`getUniformBlockElementStruct` the four shapes glslang actually emits: a `BufferBlock`-decorated struct in `Uniform` (the pre-SPIR-V-1.3 SSBO), a `Block` struct with more than one member, a sized-array member, and a matrix member with `RowMajor`/`ColMajor`/`MatrixStride`. Then arrayed bindings (an array-of-blocks pointer)~~ (done: `feme::spirv::getBufferBlockElement`/`getUniformBlockElement` -- the generalized `getBufferBlockElementArray`/`getUniformBlockElementStruct` -- now recognize glslang's own direct (unwrapped) block shape alongside FeMe's narrower upstream HLSL one: a `Uniform`-class pointer to a `BufferBlock`-decorated struct (the pre-1.3 SSBO spelling); a `Block`/`BufferBlock` struct with more than one member, with no separate wrapper to strip; and a sized-array member. `StorageBufferAccessChainPattern`/`UniformBufferAccessChainPattern` merged into one `BlockAccessChainPattern`, since both shapes reduce to the same "selector, then an ordinary GEP for anything past it" access once the selector's own index position is known. A `spirv.MatrixType` -> `llvm` dialect conversion (MLIR upstream has none at all) covers matrix members, converting to the natural column-major array-of-vectors representation LLVM's SPIRV backend itself expects; a `RowMajor` member (whose physical layout that representation cannot reproduce by reinterpreting the same bytes) or a `MatrixStride` that does not match the natural per-column stride declines the whole struct's conversion rather than miscompiling it. An array-of-blocks binding (`T blocks[N]` in GLSL) is handled by a new `ArrayedBlockAccessChainPattern`, which builds the handle itself once its own access chain's leading (array) index is available -- unlike a non-arrayed block, an arrayed one's handle needs to know *which* descriptor to bind, which the type converter alone cannot supply. See `Design.md`'s "Known gap: `spirv` dialect -> `llvm` dialect conversion coverage" for the updated scope. Whether this actually moves the 10,121-case count is unmeasured until C10 lands continuous measurement, and -- per this row's own note about C1's stacked-blocker discovery -- is only a lower bound on this step's own value in any case) | 10,121 | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P0 |
| C3 | ~~**Divergent-vector decomposition in `feme-cpu-simdize`.** The single largest *FeMe-owned* compute-track gap, already tracked as "roadmap milestone 7 deviation" by the pass's own diagnostic (§1.6). Conformance makes it P0 rather than P1: it is the first blocker for a third of all failures once C2 lands~~ (done: `feme::cpu::SIMDizePass` now decomposes a divergent `phi` of vector type (the shape a uniform diamond's merge block gives a value reconciled across two divergent arms -- the common case a `LinearizePass`-flattened conditional produces) into one per-component `phi` (`FunctionWidener::createWidenedVectorPHIStub`/`fillWidenedVectorPHIIncoming`); a `select` of vector type with a scalar `i1` condition into one per-component `select` sharing that condition (`widenVectorSelect`); a `shufflevector` at compile time into a selection among its two operands' own components, since its mask is always a compile-time constant in LLVM IR, with no runtime cost at all (`widenShuffleVector`); and a non-constant-index `extractelement` into a `select` chain over the widened index against each component's compile-time position (`widenExtractElement`), closing the "a shuffle or a dynamic index becomes selects across the components" gap FeMeCPUDesign.md's "Vectors become components, not nested vectors" always described. A `getVectorComponents` helper, shared by every one of these and by the pre-existing `insertelement`/resource-load producers, builds a uniform vector's components on demand exactly like `getWidened` does for a scalar. A `select` with a per-lane `<N x i1>` condition remains diagnosed (no shape that reaches this pass produces one, and decomposing it would need a per-component condition, not just a per-component value), and every divergent aggregate of any kind is unaffected -- see FeMeCPUDesign.md's deviation note and Roadmap.md's §1.6 table for the updated scope. `test/Transforms/CPU/simdize-vector-{phi,select,shufflevector,dynamic-extractelement}.ll` and `SIMDizeTest.{DecomposesVectorPHIAcrossUniformDiamond,DecomposesScalarConditionVectorSelect,DecomposesShuffleVectorAtCompileTime,WidensNonConstantIndexExtractElementIntoSelectChain}` cover the four newly-supported shapes; `simdize-vector-unsupported.ll` now covers the one shape (a vector-condition `select`) that remains diagnosed. Whether this actually moves the 9,067-case count is measured in VulkanCTSReport.md's "Roadmap C3: measured impact") | 9,067 | `feme/lib/Target/CPU` | P0 |
| C4 | **Graphics pipeline state breadth.** `mapTopology` beyond triangles (point, line, line-strip, fan), `mapDynamicState` beyond its six states, `FRONT_AND_BACK` culling, dual-source blend factors, and the sample counts `isSupportedAttachmentSampleCount` declines. Every one of these is a rasterizer/executor feature, not a translation gap, so this is really G-track work surfaced by the Vulkan track. ~~**Sub-step C4a, do first and separately: make every silent rejection diagnose itself.** A state-side rejection currently emits nothing at all, so triaging this bucket means reading `GraphicsPipeline.cpp` instead of the ICD's output~~ (C4a done: `feme::vulkan::logCreationFailure` (`feme/lib/Vulkan/Diagnostics.h`/`.cpp`) is the opt-in log callback FeMeVulkanDesign.md's "Error Handling and Security" section already named ahead of it existing; `vkCreateGraphicsPipelines` calls it instead of a bare `consumeError`, printing the discarded `llvm::Error`'s message when `FEME_VULKAN_LOG_CREATION_ERRORS` is set in the host environment and staying silent otherwise, so triaging a state-side rejection no longer requires reading this file's source -- see FeMeVulkanDesign.md's updated bullet for the full deviation note, including why an environment variable rather than `VK_EXT_debug_utils` itself. C4b done in part: `CullMode::FrontAndBack` (culls every primitive, matching `VK_CULL_MODE_FRONT_AND_BACK`'s "no primitive of the pipeline's type is rasterized" semantics) and 8x multisampling (`samplePositions`' documented "mechanical, on-demand addition of another row" in `Executor.cpp`, `isSupportedAttachmentSampleCount` in `RenderPass.cpp`, and the matching `VK_SAMPLE_COUNT_8_BIT` advertisement in `PhysicalDeviceInfo.cpp`) are both implemented and tested. ~~`mapDynamicState` beyond its six states... each needs new rasterizer primitives... that are a materially larger, separate unit of G-track work rather than a mechanical table addition~~ (C4c done, and this framing turned out to be wrong for dynamic state specifically: every one of `VK_EXT_extended_dynamic_state`'s 12 dynamic states -- cull mode, front face, depth test/write/compare-op, depth-bounds-test-enable, stencil test-enable/op, viewport/scissor "with count", primitive topology restricted to the triangle class already implemented, and vertex-input-binding-stride via `vkCmdBindVertexBuffers2EXT` -- already had a complete *static* path or a bounded, mechanical extension of one before this milestone, so all 12 are now implemented and the extension is advertised (`feme/lib/Vulkan/{GraphicsPipeline,CommandBuffer,PhysicalDeviceInfo,EntryPoints}.{h,cpp}`, `feme/utils/vk_gen_entrypoints.py`) -- see FeMeGraphicsDesign.md's updated status note for the full per-state breakdown. ~~`mapTopology` beyond `TriangleList`/`TriangleStrip`... each needs new rasterizer primitives (point/line assembly and width, a second fragment-stage color output) that are a materially larger, separate unit of G-track work rather than a mechanical table addition~~ (C4d done, and this framing held for only one of the four remaining topologies: `TriangleFan` needed no new rasterizer primitive at all, just a different per-primitive vertex-index assembly sharing the existing triangle clip/rasterize path; point and line topologies do need a new primitive shape, but `executeDraws` gets one by expanding each into a two-triangle screen-space quad and feeding it through that same existing path, fixed at a 1-pixel point size/line width since `largePoints`/`wideLines` are not advertised device features -- see FeMeGraphicsDesign.md's updated status note for the full breakdown, including the one accepted deviation (no side-plane clip for points/lines, only a whole-primitive near-plane reject). ~~Only the dual-source blend factors remain open: they need a second fragment-stage color output that is a materially larger, separate unit of G-track work rather than a mechanical table addition~~ (C4e done: the missing piece was narrower than that framing suggested -- the `Index` SPIR-V decoration already survived `spirv` -> `llvm` conversion unmodified, so the only real gap was `SignatureElement` gaining an `Index` field and `CanonicalizeStage.cpp` reflecting the decoration into it; `executeDraws` now reads a fragment stage's `Index=1` output at `Location=0` for `VK_BLEND_FACTOR_SRC1_*`, and `dualSrcBlend` is an advertised device feature -- see FeMeGraphicsDesign.md's updated status note for the full breakdown). Roadmap C4 is now closed in full. | 3,354 | `feme/lib/Vulkan/GraphicsPipeline.cpp`, `feme::graphics` | P0 |
| C5 | **Mandatory API object model.** ~~Occlusion queries in `vkCreateQueryPool` (mandatory in 1.0)~~ (done: `feme::vulkan::QueryPool` now accepts `VK_QUERY_TYPE_OCCLUSION` and accumulates the exact per-sample depth/stencil-test-pass count across draws recorded between `vkCmdBeginQuery`/`vkCmdEndQuery`, reusing V6's real rasterizer coverage and test results; timestamp queries remain truthful zero-valued results, and `VK_QUERY_TYPE_PIPELINE_STATISTICS` still declines because this ICD has no truthful counter for it); ~~the descriptor types `isSupportedDescriptorType` declines (input attachment, dynamic uniform/storage buffer)~~ (done: the dynamic uniform/storage buffer types were already present; the only remaining gap, `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT`, is now accepted and materialized as the same read-only image-view+layout record as `SAMPLED_IMAGE`); ~~the `VkRenderPass` shapes `feme::vulkan::RenderPass` declines (resolve and input attachments, multi-subpass dependencies)~~ (done: resolve attachments and multi-subpass execution were already in place; `vkCreateRenderPass`/`vkCreateRenderPass2` now also accept and retain input-attachment references, and validate subpass dependencies -- including out-of-range `srcSubpass`/`dstSubpass` and `VkSubpassDependency2::viewOffset`/`VK_DEPENDENCY_VIEW_LOCAL_BIT` -- before collapsing them to this ICD's existing full-join semantics); ~~the `VkSubgroupFeatureFlags` contradiction (`BASIC_BIT` must be set whenever a graphics or compute queue exists)~~ (done: both `VkPhysicalDeviceSubgroupProperties` and the promoted `VkPhysicalDeviceVulkan11Properties` chain now report the same compute-stage `VK_SUBGROUP_FEATURE_BASIC_BIT` baseline); `VkPhysicalDeviceDriverProperties` remains only partially closed (done: the struct and the promoted `VkPhysicalDeviceVulkan12Properties` chain are now queryable, with non-empty null-terminated `driverName`/`driverInfo` strings and a truthful zero `VkConformanceVersion`; remaining deviation: FeMe still has no Khronos-assigned `VkDriverId`, so `driverID` stays `VK_DRIVER_ID_MAX_ENUM` rather than impersonating another driver, and the CTS `api.driver_properties.conformance_version` case still fails on the deliberate zero) | 558 | `feme/lib/Vulkan/{QueryPool,Descriptor,RenderPass,PhysicalDeviceInfo}.cpp` | P0 |
| C6 | ~~**Mandatory 1.2 features and limits.** Only if the version decision above keeps 1.2: `multiview`, `imagelessFramebuffer`, `uniformBufferStandardLayout`, `separateDepthStencilLayouts`, `hostQueryReset`, `shaderSubgroupExtendedTypes`, `subgroupBroadcastDynamicId`, plus raising `maxTimelineSemaphoreValueDifference` and `maxMemoryAllocationSize` to their required minimums and fixing the `vkGetPhysicalDeviceFeatures2`-versus-promoted-struct disagreements~~ (done, with one deliberate exception: `hostQueryReset` (`vkResetQueryPool` already existed), `uniformBufferStandardLayout`/`separateDepthStencilLayouts` (neither restriction they relax was ever enforced), `shaderSubgroupExtendedTypes`/`subgroupBroadcastDynamicId` (vacuously true -- no `OpGroupNonUniform*` operation is converted at all yet), and `imagelessFramebuffer` (`vkCreateFramebuffer`/`vkCmdBeginRenderPass` now defer a framebuffer's attachment views to `VkRenderPassAttachmentBeginInfo`, needing no layered-rendering support since it stays single-layer) are all now advertised and implemented; `maxMemoryAllocationSize`, `maxPerSetDescriptors`, `maxMultiviewViewCount`/`maxMultiviewInstanceIndex`, and `maxTimelineSemaphoreValueDifference` (`UINT64_MAX`) are raised to or above their required minimums in both their dedicated structs and the promoted `VkPhysicalDeviceVulkan11Properties`/`VkPhysicalDeviceVulkan12Properties` chains, closing the disagreement between them. **`multiview` stays unadvertised**: it needs layered rendering (roadmap V7, not yet implemented -- `vkCreateFramebuffer`'s `layers != 1` and `vkCreateRenderPass2`'s `viewMask != 0` rejections are both untouched), so the feature bit alone would be untruthful even though `maxMultiviewViewCount`/`maxMultiviewInstanceIndex` must still report real minimums once the advertised API version is >= 1.2, regardless of `multiview` itself -- see FeMeVulkanDesign.md's updated "Limits and features" and "Render passes and dynamic rendering" status notes, and VulkanCTSReport.md's "Roadmap C6: measured impact" for the CTS effect) | 28 | `feme/lib/Vulkan/PhysicalDeviceInfo.cpp`, V7 | P1 |
| C7 | ~~**Queue family capability combinations.** 99,324 cases report `NotSupported` because no queue family matches a required capability set. A family advertising `GRAPHICS` must also advertise `TRANSFER`; the mandatory combinations must all be coverable. These are `NotSupported`, not `Fail`, today, so they cost nothing in the failure count -- but a conformance run that declines a hundred thousand mandatory cases is not a conformance run~~ (done: the universal family already advertised `GRAPHICS`-implies-`TRANSFER`; the actual gap was that no family *excluded* graphics/compute at all, which several mandatory `dEQP-VK` cases require (e.g. `dEQP-VK.pipeline.*.timestamp.transfer_tests.*_transfer_queue`, `dEQP-VK.api.buffer_marker.compute.*`). `feme::vulkan::PhysicalDeviceInfo` now reports two more, narrower families alongside the universal one -- a `TRANSFER`-only family and a `COMPUTE | TRANSFER`-only family, both excluding `GRAPHICS` -- neither of which claims an independent execution engine the single worker pool does not have; each only restricts what one logical submission queue promises to accept. `vkCreateDevice`/`vkCreateCommandPool` now validate against `PhysicalDeviceInfo::NumQueueFamilies` instead of hardcoding "only family 0 exists". Measured impact: every `findQueueFamilyIndexWithCaps`-shaped `NotSupported` this pass could reach (`dEQP-VK.pipeline.monolithic.timestamp`, `api`, `synchronization`, `synchronization2`, `renderpasses`, `sparse_resources`, `fragment_shading_rate`) dropped to zero, save two cases needing a dedicated video-decode queue -- correctly `NotSupported`, since video extensions are not advertised at all; see VulkanCTSReport.md's "Roadmap C7: measured impact". Reaching further also surfaced and closed one CTS-side null-function-pointer crash (`dEQP-VK.api.copy_and_blit.copy_commands2.image_to_image_transfer_queue.misc.ms_then_ss`, a real, previously-unreachable local CTS gap -- see "Deviations from a stock CTS") | 0 (99,324 `NotSupported`) | `feme/lib/Vulkan/PhysicalDeviceInfo.cpp` | P1 |
| C8 | **Shader long tail.** Descriptor arrays of combined image samplers (816), matrix/aggregate stage IO (309), the SPIR-V importer's `unhandled opcode` set (171), `Workgroup` arrays-of-arrays (151), the 277 individually-unlegalized ops (`spirv.SpecConstant`, `spirv.VectorExtractDynamic`, `spirv.CompositeConstruct`, the `spirv.Atomic*` family, ...), and the 242-case diagnostic tail. Best attacked *after* C2/C3, since the true size of this bucket is unknown until the stacked blockers ahead of it are gone. ~~**C3's own measurement found a new member of this bucket, larger than any row already in it**: a plain, non-atomic `load`/`store` on a raw SPIR-V `Input`/`Output`-storage-class global (address space 7/8, see `getStageIOAddressSpace` in SPIRVToLLVMPatterns.cpp) is never canonicalized into the `feme.stage.*` calls `feme::cpu::LinearizePass`/`SIMDizePass` already know how to widen the way a DXIL/HLSL-imported shader's stage IO always is (`feme::dxil::OpRaisingPass`) -- so a SPIR-V-imported fragment/vertex shader's own divergent output store hits `feme-cpu-simdize`'s vector-decomposition diagnostic (or, for a scalar output, presumably a similar unmasked-side-effect gap) not because vector decomposition itself is incomplete, but because the value it is being asked to decompose was never routed into the mechanism that already knows what to do with it. This is a *different* root cause from C3's own scope (which is genuinely closed -- see FeMeCPUDesign.md's deviation note) and from this row's existing matrix/aggregate-stage-IO entry (a `spirv`-\>`llvm` *conversion* gap, not a CPU-target *raising* gap); it is the largest single reason C3's own headline barely moved despite closing every producer/consumer shape "Vectors become components" describes~~ (done: the actual root cause was narrower, and more general, than this finding's own framing -- it was never specific to SPIR-V. `feme::graphics::CanonicalizeStagePass` (the pass that rewrites both a DXIL `loadInput`/`storeOutput` call *and* a raw SPIR-V `Input`/`Output`-storage-class global load/store into `feme.stage.*`) was never run by `feme::cpu::runPipeline` *at all* -- only by the separate Vulkan graphics pipeline (`GraphicsPipeline.cpp`), which every real CTS draw call goes through, but which the CPU-target pipeline `feme-run`/`feme-cpu-simdize` measure against does not. A DXIL-imported fragment/vertex shader reaching this pipeline directly hit the exact same gap SPIR-V's did (`checkSupportedRaisedOps` diagnosing the still-raw `dx.op.loadInput`/`storeOutput` call outright, rather than `feme-cpu-simdize`'s vector-decomposition diagnostic reaching a raw store the way SPIR-V's did) -- the roadmap text's framing of DXIL's stage IO as "always" canonicalized this way did not hold once traced through this pipeline specifically, only through the Vulkan one. `runPipeline` now runs `CanonicalizeStagePass` immediately before `ValidateStagePass`, matching the ordering "CPU Lowering Pipeline"'s own diagram in FeMeGraphicsDesign.md already drew, closing this gap for both import paths with one change (`feme/lib/Target/CPU/Pipeline.cpp`); see that document's updated status note for the full deviation record and `unittests/Target/CPU/PipelineTest.cpp`'s `CanonicalizesRaw{SPIRV,DXIL}StageIOBeforeWidening` for the two regression tests, each first confirmed failing (in its own distinct way) against the pre-fix pipeline. Measured against a real `deqp-vk` run, this fix moves **nothing**: `feme::vulkan::compileGraphicsStage` (`GraphicsPipeline.cpp`) already calls `CanonicalizeStagePass` directly, since roadmap V6, before every real `vkCreateGraphicsPipelines` call reaches `runPipeline` at all, so no `dEQP-VK` case was ever routed through the gap this fix closes -- it only mattered for `feme::cpu::JITEngine`/`feme-run`'s direct entry points. The C3 section's own attribution of its stalled headline to this finding does not hold once measured, and the rest of C8's bucket -- descriptor arrays of combined image samplers, matrix/aggregate stage IO, the remaining `spirv`-\>`llvm` conversion gaps, and the broader unlegalized-op/diagnostic tail -- remains open, exactly as large as before; see VulkanCTSReport.md's "Roadmap C8: measured impact" for the full before/after comparison) | 1,966 | mixed, mostly `feme/lib/Conversion/SPIRVToLLVM` and `mlir/lib/Target/SPIRV` | P1 |
| C9 | **Upstream the CTS fix.** `dEQP-VK.api.invariance.random` segfaults on any narrow-format ICD because CTS's own `ImageAllocator` indexes an empty format vector; the fix is applied locally to the VK-GL-CTS checkout today. A conformance submission must run a stock CTS, so this must land in VK-GL-CTS itself | 0 | VK-GL-CTS | P1 |
| C10 | **Continuous measurement.** Nothing in this tree runs the CTS automatically: `feme/test/Vulkan/cts-compute-subset.test` is gated on `REQUIRES: system-vulkan-cts` and covers a compute subset only. A conformance push needs the full 54-group run, its per-case root-cause attribution, and a checked-in expected-failure list, so that a regression is a test failure rather than a re-reading of this report | 0 | `feme/utils`, `feme/test/Vulkan` | P1 |

Sequencing: C1 first (cheapest, largest end-to-end unlock, and a hard
conformance requirement rather than a scope choice), then C4a (so the
remaining triage is readable), then C2 and C3 in parallel (different
subsystems, and jointly ~70% of the failure count), then C4, C5 and C7,
then C6 if the 1.2 claim survives, then C8 measured afresh, with C9 and C10
running alongside from the start. C10 is what keeps this report from
needing to be regenerated by hand again.

What is *not* on this list, deliberately: WSI/presentation, ray tracing,
mesh shading, tessellation, geometry, sparse resources, YCbCr,
transform feedback, `VK_EXT_shader_object` and cooperative matrix/vector.
Every one is optional for Vulkan 1.2 conformance, every one is correctly
reported `NotSupported` today, and each remains scheduled by its own V or G
milestone rather than by conformance.

#### 1.9.2 The road to Vulkan 1.4 conformance

§1.9.1's own framing decision -- "Conformance target version... This
decision should be made before C1" -- was answered by keeping 1.2
throughout C1-C8. Roadmap D0 revisits it: the advertised `apiVersion` is
now `VK_API_VERSION_1_4`, ahead of C1-C8's own discipline of only
advertising a version once its mandatory surface was actually
implemented (1.0 -> 1.1 -> 1.2 each happened that way; 1.2 -> 1.4 did
not). This section is the plan that inversion now needs, mirroring
§1.9.1's own shape but starting from an honest accounting of what a
version-ahead-of-implementation claim costs, measured rather than
assumed.

| # | Step | Owner | Priority |
|---|---|---|---|
| D0 | ~~**Advertise `VK_API_VERSION_1_4`, and fix whatever it immediately breaks.**~~ (done: `vkEnumerateInstanceVersion`/`VkPhysicalDeviceProperties::apiVersion` now report 1.4. This alone made `deqp-vk` exercise `VK_KHR_copy_commands2`'s six commands for the first time -- they have no feature-bit gate of their own, so a device claiming `apiVersion >= 1.3` is assumed to implement them unconditionally -- and neither the pre-promotion `KHR`-suffixed names nor the promoted core names existed in this ICD at all, producing a real segfault. Fixed as part of this same step: all six now delegate to their already-implemented, already-tested non-`2` counterparts. A second, distinct crash was found and left open by this step -- see D2) | `feme/lib/Vulkan/{EntryPoints,PhysicalDeviceInfo,CommandBuffer}.cpp`, `vk_gen_entrypoints.py`; VulkanCTSReport.md's "Roadmap D0: measured impact" | P0 |
| D1 | ~~**An accurate 1.3/1.4 mandatory-feature/limit/extension inventory.** `vk_gen_entrypoints.py`'s `CORE_FEATURES` now resolves through `VK_VERSION_1_3` (D0), but `VK_VERSION_1_4` is not yet included, and no promoted-1.3/1.4 feature struct (`VkPhysicalDeviceVulkan13Features`/`Vulkan14Features` and their per-extension originals -- `dynamicRendering` is already advertised via its pre-promotion `VK_KHR_dynamic_rendering` path, but `synchronization2`, `maintenance4`/`5`/`6`, `subgroupSizeControl`, `shaderIntegerDotProduct`, `pipelineCreationCacheControl`, `pushDescriptor`, and the rest are not) has been audited against what claiming 1.4 actually requires. This is D0's own "measure honestly" step turned into a checklist: enumerate the full set from `vk.xml` itself (the same way `vk_gen_entrypoints.py` already resolves `CORE_FEATURES` transitively), rather than re-deriving it by hand the way C6 did for 1.2's much shorter list~~ (done: `CORE_FEATURES` now includes `VK_VERSION_1_4` itself, closing the entrypoint-table coverage gap this row's own premise named -- purely a coverage fix, since an unlisted name and a listed-but-unimplemented one both already resolved to null via `ProcAddr.cpp`'s `findEntry`. The actual audit is a new tool, `feme/utils/vk_gen_feature_inventory.py`, enumerating every `VkPhysicalDeviceVulkan{13,14}Features`/`...Properties` struct member and every extension `vk.xml` records as `promotedto` that version, cross-checked against a checked-in `feme/lib/Vulkan/AdvertisedPromoted{Features,Extensions}.txt`; see `feme/docs/Vulkan14FeatureInventory.md` for the generated checklist and `FeMeVulkanDesign.md`'s updated "Limits and features" status note for the summary. Headline: of 1.3/1.4's 36 mandatory feature bits, only `dynamicRendering` is genuinely implemented (and only through its pre-promotion `VK_KHR_dynamic_rendering` struct, not yet the aggregate one); all 70 mandatory limit fields are unenumerated (neither promoted `...Properties` struct has a `vkGetPhysicalDeviceProperties2` case); of 39 promoted extensions, only the 2 already advertised are implemented. This milestone is the inventory itself, not closing what it found -- see the doc's own "Findings" for the full per-row breakdown that future roadmap rows will triage against) | `feme/utils/vk_gen_entrypoints.py`, `feme/lib/Vulkan/PhysicalDeviceInfo.cpp` | P0 |
| D2 | ~~**The system Vulkan loader crash D0's second CTS pass found.** `dEQP-VK.api.object_management.multithreaded_per_thread_resources.*`, run as one sequence, segfaults inside Ubuntu's `libvulkan1` (`vkGetDeviceProcAddr`, called from concurrent `vkCreateDevice`s) -- not inside any FeMe code, and does not reproduce against the pre-D0 (apiVersion 1.2) build. Characterize further (does it reproduce with a *smaller* `apiVersion`-dependent entrypoint table than 1.4's full one? does a newer/older `libvulkan1` avoid it?) and, if confirmed as a loader bug rather than something this ICD's own dispatch-table generation can influence, file it upstream rather than attempt a local workaround in a component this project does not own~~ (done: confirmed as a race (3/5 repeated runs crash, not 5/5), with a `gdb` backtrace identical to D0's (`vkGetDeviceProcAddr` <- `vk::DeviceDriver::DeviceDriver` <- a `ThreadGroupThread` constructing its own `VkDevice`, no FeMe frame anywhere). Trimming `CORE_FEATURES` back to `VK_VERSION_1_3` (a smaller generated table) and rebuilding `feme_vulkan` did **not** reduce the crash rate against the same system loader (4/5 still crashed) -- ruling out table size as the deciding variable and superseding D0's "larger dispatch table" guess. Traced to an *already-tracked, already-fixed* upstream defect, KhronosGroup/Vulkan-Loader#1436/#1438 (a `-fno-strict-aliasing`-removal regression in the loader's own Release-build GPA code, fixed in loader tag `v1.3.277`): building loader `v1.3.280` from source cut the rate to 1/6, and building current upstream `main` (v1.4.360-era, with further mutex hardening since `v1.3.280`) eliminated it across 10/10 runs. Ubuntu 24.04's `libvulkan1` package is pinned to the pre-fix `1.3.275.0` with no newer candidate in `noble`/`noble-updates`/`noble-backports`. Not filed as a *new* upstream issue -- it would duplicate the already-closed #1436 -- since the actionable gap is Ubuntu's packaging lag, not anything left for Vulkan-Loader or this ICD to fix; see VulkanCTSReport.md's "Roadmap D2: measured impact" for the full loader-build comparison table and backtraces) | VulkanCTSReport.md's "Roadmap D2: measured impact" | P1 |
| D3 | ~~**Per-bucket attribution of D0's net +2,553 newly-failing cases**, at C1-C8's level of rigor. One bucket is already traced (`ubo.*.std430`'s 2,650 cases: a pre-existing, already-tracked `feme-cpu-simdize` divergent-vector-decomposition gap, C3's own "milestone 7 deviation", newly *reached* rather than newly created); `spirv_assembly.instruction.compute` (417), `synchronization.op.{multi,single}_queue` (277), and the remaining tail are not yet traced~~ (done, with a correction: re-running the *exact* pre-D0/post-D0 commits against the currently-checked-out CTS (which has since drifted one local commit past what D0's own report cites) does **not** reproduce a net +2,553 -- it reproduces a net **-417** (525 newly-`Fail`, 942 no-longer-`Fail`), with every group individually attributed. Two of D0's three named buckets do not survive re-verification at the same rigor D0 itself used for `ubo.*.std430`: `ubo.*.std430` itself is **not** apiVersion-gated at all -- `feme-cpu-simdize`'s divergent-vector diagnostic fires identically at 1.2 and 1.4 (a byte-identical `ubo` group log both ways), so its 2,650 cases were already failing pre-D0 and this bucket should never have been attributed to the version bump; `synchronization.op.{multi,single}_queue` likewise reproduces an *identical* 498-case failing set at both versions (confirmed deterministic across repeated isolated runs), so its 277-case attribution does not hold up either. `spirv_assembly.instruction.compute` **does** reproduce (422 cases, close to the original 417): rooted to `SPIRVToLLVMPatterns.cpp`'s `ImageFetchPattern`/`ImageFetchLodPattern` (and the analogous `ImageSampleExplicitLod` patterns) requiring an exact image-operands match and having no case at all for the SPIR-V 1.6 `Nontemporal` hint bit deqp-vk's SPIR-V-1.6-only (`apiVersion >= 1.3`) shaders now emit -- a cache hint with no correctness effect, currently unhandled anywhere in this ICD. Three further newly-failing buckets D0 did not name: `graphicsfuzz` (72, `VK_KHR_shader_terminate_invocation` assumed implemented once promoted-to-1.3, per D1's inventory it is not); `dEQP-VK.api.info.*`'s own `vulkan1p3`/`get_physical_device_properties2` consistency checks (18, exactly the "vulkan1p3_consistency" shape D0's first draft guessed and discarded -- it materializes in `api.info.*`, not the separate top-level `info` group D0 actually checked); `compute.pipeline.zero_initialize_workgroup_memory` (7, same "promoted extension assumed implemented" shape as `graphicsfuzz`, per D1's inventory); and `robustness.oob_access` (6, a texel-buffer-format/robustness mismatch). A large, undercounted no-longer-failing side (942 cases -- D0's report never measured a complete `api` group, so its 1,999 figure could not have included this) turned out to be a good-news case of the same D1-tracked gap: `draw`/`renderpasses`/`pipeline`'s `dynamic_rendering` cases (613 + 204 + 5) go from a crash-adjacent `VK_ERROR_INITIALIZATION_FAILED` Fail to a correctly-truthful `NotSupported`, because deqp-vk's own support check for `VK_KHR_dynamic_rendering` consults `VkPhysicalDeviceVulkan13Features.dynamicRendering` once `apiVersion >= 1.3`, and FeMe's `vkGetPhysicalDeviceFeatures2` has no case at all for that blob struct's type (D1's own finding), so it reads back `false` even though `dynamicRendering` is truthfully advertised through its pre-promotion struct. See VulkanCTSReport.md's "Roadmap D3: measured impact" for the full per-bucket table, root causes, and the reproducibility caveat) | VulkanCTSReport.md's "Roadmap D3: measured impact" | P1 |
| D4 | **Continuous, crash-tolerant measurement**, generalizing roadmap C10: this pass's own two full runs each needed a hand-rolled Python pass over raw `.log` files to diff two runs' actual per-case result sets against each other, since a single crashed group's totals silently corrupt naive count-only comparisons (see D0's own "diffing the two runs' actual per-case result sets, not just the aggregate counts" step) | `feme/utils`, `feme/test/Vulkan` | P1 |

Sequencing: D1 first (an inventory is a prerequisite for every subsequent
1.3/1.4 feature-implementation row this section will eventually need, none
of which are listed yet since D1 hasn't produced them), D2 and D3 alongside
it (independent of D1, and each already has a concrete lead), D4 whenever
time allows since it only pays off on the *next* full run. Every row this
section will need beyond D1's own inventory -- implementing
`synchronization2`, `maintenance4`/`5`/`6`, `dynamicRendering`'s core
(non-`KHR`) names, and the rest of §1.9.2's actual conformance-floor work
-- is deliberately not listed yet, the same way C1-C8's own rows were not
listed until §1.9.1 existed to schedule them against a similarly-measured
baseline.

D1, D2 and D3 are now done, and §1.9.2's own promise -- "every row this
section will need beyond D1's own inventory is deliberately not listed yet"
-- is what §1.9.3-§1.9.5 below deliver: the actual, distributable task
breakdown, derived directly from `Vulkan14FeatureInventory.md`'s findings
and D3's regression attribution, at a granularity intended for
parallel work by independent agents and humans rather than a single
sequential owner.

#### 1.9.3 Framing decision this breakdown does not get to skip

§1.9.1 flagged "conformance target version... should be made before C1" and
C1-C8 honored it: 1.0 -> 1.1 -> 1.2 each only advertised once its floor was
implemented. D0 broke that discipline (apiVersion jumped straight to 1.4
ahead of any 1.3/1.4 feature work), and D1's inventory is the honest
accounting of what that costs: **36 mandatory feature bits with 1
implemented, 70 mandatory limit fields with 0 wired, 39 promoted
extensions with 2 implemented.** D3 additionally found that the version
bump alone made three previously-passing buckets regress into `Fail`
(`spirv_assembly.instruction.compute`, `graphicsfuzz`,
`compute.pipeline.zero_initialize_workgroup_memory`, 501 cases total) by
making `deqp-vk` assume promoted-but-unimplemented capabilities are real.

This roadmap does **not** revert the version bump -- undoing D0 would
throw away D1-D3's own measurements and restart the same climb from a
different number -- but every row below is written so a distributor can
choose either of two strategies without redoing the inventory:

- **Strategy A (recommended): finish §1.9.4 (the full 1.3 floor) before
  starting any §1.9.5 (1.4-only) row**, actually reaching 1.3 conformance
  as a real, submittable milestone before continuing to 1.4, mirroring
  C1-C8's own "advertise what's implemented" discipline one level higher
  instead of abandoning it a second time.
- **Strategy B: run both tracks in parallel** if the distributor has
  enough independent agents/humans that §1.9.4 and §1.9.5 do not compete
  for the same reviewer bandwidth -- the two tracks touch almost entirely
  disjoint files (§1.9.5's rows are new `lib/Vulkan/*` cases and new
  `EntryPoints.cpp` command stubs; only E1/E2's struct plumbing is a
  shared dependency both tracks reuse, so it should land first regardless
  of which strategy is chosen).

Either way, **E1 and E2 below are the one true prerequisite**: they are
the aggregate feature/limit struct plumbing every other row in both
§1.9.4 and §1.9.5 registers itself into, the same "one small addition,
every subsequent row plugs into it" role R11's `Diagnostic`/`Exporter`
scaffolding played for §1.1-§1.5.

#### 1.9.4 Closing the Vulkan 1.3 mandatory floor (E-series)

Each row is independently assignable: one agent or human, one feature (or
a tightly-coupled pair sharing one code path), one commit series, one
lit/unittest addition, following `.instructions.md`'s "small, individually
testable" rule. "CTS" cites the specific regression D3 traced to that gap,
where one is known; most rows have no known regression today because the
capability is simply unadvertised (`NotSupported`, not `Fail`) rather than
lied about, so their CTS payoff is newly-*passing* mandatory cases, not
newly-fixed failures, and is unmeasured until each row's own CTS run.

| # | Task | Depends on | Files | Priority |
|---|---|---|---|---|
| E1 | ~~**Wire the aggregate `VkPhysicalDeviceVulkan13Features`/`Vulkan14Features` `vkGetPhysicalDeviceFeatures2` cases.** Today only the pre-promotion `VK_KHR_dynamic_rendering` struct case exists; add the two aggregate cases, populating every member from whatever per-extension state already exists (`dynamicRendering=VK_TRUE` today) and `VK_FALSE` for everything still unimplemented, exactly mirroring the existing `VULKAN_1_2_FEATURES` case's pattern. This is what `dEQP-VK.api.info.*`'s consistency checks and every `VK_KHR_dynamic_rendering`-support probe (`draw`/`renderpasses`/`pipeline`, 822 cases) actually read~~ (closed: both aggregate cases now exist in `EntryPoints.cpp`, mirroring `VULKAN_1_2_FEATURES`'s explicit-every-field pattern; `dynamicRendering=VK_TRUE`, every other 1.3/1.4 bit `VK_FALSE`) | none | `feme/lib/Vulkan/EntryPoints.cpp` | P0 |
| E2 | ~~**Wire the aggregate `VkPhysicalDeviceVulkan13Properties`/`Vulkan14Properties` `vkGetPhysicalDeviceProperties2` cases**, enumerating all 70 mandatory limit fields from `Vulkan14FeatureInventory.md`'s table. Most are either a real minimum this ICD can already compute (e.g. `maxBufferSize`, `storageTexelBufferOffsetAlignmentBytes`) or a truthful `VK_FALSE`/`0` for a capability not yet implemented (every `integerDotProduct*Accelerated` bit until E8 lands, `maxPushDescriptors` until F12 lands). Land the struct case with every field set to a conservative, honest value first, then let each later row (E8, F5, F11, F12, ...) raise its own subset once the feature behind it is real, instead of blocking this row on every other one~~ (closed, with a correction to this row's own premise: both aggregate cases now exist in `EntryPoints.cpp` -- following `VULKAN_1_2_PROPERTIES`'s own explicit-every-field pattern -- with every one of the 46 (1.3) + 25 (1.4) fields written, but *every* field is the conservative `0`/`VK_FALSE`/`nullptr`, not a mix of real minima and placeholders as this row originally proposed. A first draft did compute real values for a handful of fields (`maxBufferSize`, `storageTexelBufferOffsetAlignmentBytes`, `minSubgroupSize`, ...) from state this ICD already tracks; a targeted CTS run caught the reason that was wrong before landing it: `dEQP-VK.api.info.vulkan1p3/1p4.property_extensions_consistency` cross-checks *every* aggregate-struct field against its own pre-promotion, per-extension dedicated struct (`VkPhysicalDeviceSubgroupSizeControlProperties`, `VkPhysicalDeviceTexelBufferAlignmentProperties`, `VkPhysicalDeviceMaintenance4Properties`, `VkPhysicalDeviceLineRasterizationPropertiesKHR`, ...) once apiVersion >= 1.3/1.4, the same way `vulkan1p3.feature_extensions_consistency` cross-checked `dynamicRendering` before E1 -- and none of those dedicated structs has its own `Properties2` case yet, so they all still read back zero. A real, nonzero aggregate-struct value disagrees with that zero and regresses a currently-passing consistency case instead of closing one. See "Roadmap E2: measured impact" in VulkanCTSReport.md for the full before/after. Each field's own later row (E4, E6, E7, E8, E18, F5, F6, F8, F10, F11, F12) is therefore the one that gets to raise it, in lockstep with adding that row's own dedicated-struct case) | none | `feme/lib/Vulkan/EntryPoints.cpp` | P0 |
| E3 | ~~**`VK_KHR_synchronization2`/`synchronization2`.** `vkCmdPipelineBarrier2`/`vkCmdWriteTimestamp2`/`vkQueueSubmit2`/`vkCmdSetEvent2`/`vkCmdResetEvent2`/`vkCmdWaitEvents2` translate `VkDependencyInfo`'s per-resource `VkMemoryBarrier2`/`VkBufferMemoryBarrier2`/`VkImageMemoryBarrier2` (2-stage-mask, 2-access-mask shape) down to the existing 1-mask `Sync.{h,cpp}` model, the same "new entrypoint, old backing model" pattern C7 used for queue families~~ (done, with a correction to this row's own premise: all six commands were already core, non-`KHR`-suffixed `VK_VERSION_1_3` entries in `vk_gen_entrypoints.py`'s generated table -- like `VK_KHR_copy_commands2` before it (roadmap D0), this needed no `vk_gen_entrypoints.py`/`SUPPORTED_EXTENSIONS` change, only `ImplementedEntrypoints.txt` entries and real `CommandBuffer.cpp`/`Sync.cpp` bodies. Each image barrier's layout transition (`vkCmdPipelineBarrier2`) and each event/wait-events/write-timestamp command translates straight down to the identical payload its non-`2` counterpart already produces; `vkQueueSubmit2`'s per-element `VkSemaphoreSubmitInfo`/`VkCommandBufferSubmitInfo` structs translate down to the same `Fence`/`Semaphore`/`CommandBuffer` execution `vkQueueSubmit` itself now shares through two small `Sync.cpp`-local helpers. Unlike D0's precedent, though, `getSupportedDeviceExtensions` *does* need `VK_KHR_synchronization2` added -- `dEQP-VK.synchronization2`'s own multi-queue/custom-device cases enable it by name at `vkCreateDevice` regardless of the advertised `apiVersion`, discovered by a targeted CTS run before landing (see "Roadmap E3: measured impact" in VulkanCTSReport.md). `EntryPoints.cpp` also gained the dedicated `VkPhysicalDeviceSynchronization2Features` case, agreeing with `synchronization2 = VK_TRUE` in the aggregate `VkPhysicalDeviceVulkan13Features` struct E1 already wired, closing the last `api.info` consistency gap D1's inventory found for this bit)~~ | E2 (for the feature bit) | `feme/lib/Vulkan/Sync.{h,cpp}`, `CommandBuffer.cpp`, `EntryPoints.cpp`, `PhysicalDeviceInfo.cpp` | P0 |
| E4 | ~~**`VK_KHR_maintenance4`/`maintenance4`.** `vkGetDeviceBufferMemoryRequirements`/`vkGetDeviceImageMemoryRequirements`/`vkGetDeviceImageSparseMemoryRequirements` compute requirements from a `VkBufferCreateInfo`/`VkImageCreateInfo` directly (no live object needed) by factoring the existing `vkCreateBuffer`/`vkCreateImage` sizing logic into a shared helper both the live and the info-only entrypoint call; also relaxes `vkCreateShaderModule`'s local-size-id/local-size validation and `VK_KHR_maintenance4`'s zero-size-descriptor-array rule~~ (closed, with a correction to this row's own two "relaxes" clauses: the zero-size-descriptor-array rule needed no change at all -- `vkCreateDescriptorSetLayout`/`vkAllocateDescriptorSets` already accept `descriptorCount == 0`, closed by a regression test alone (`DescriptorTest.AcceptsZeroSizeReservedBinding`) -- and `GroupSize.cpp`'s own `LocalSizeId` resolution was *also* already correct and unit-tested (`GroupSizeTest.ResolvesFromLocalSizeIdDefaults`); what actually needed relaxing, found only once `PipelineTest.CompilesLocalSizeIdComputeShader` exercised the whole compile pipeline rather than `GroupSize.cpp`'s raw-word scan alone, was `SPIRVToLLVMPatterns.cpp`'s legalization: neither `spirv.SpecConstant` (`LocalSizeId`'s only way to spell its operands) nor `spirv.ExecutionModeId` has an upstream MLIR conversion pattern, so a new `ExecutionModeIdPattern`/`SpecConstantErasurePattern` pair erases both, mirroring the existing `ExecutionModePattern` precedent for plain `LocalSize`. The three new entrypoints themselves landed as designed, sharing `isValidBufferCreateInfo`/`computeBufferMemoryRequirements` (Buffer.cpp) and `isValidImageShape`/`computeImageCreateInfoSize`/`fillImageMemoryRequirements` (Image.cpp) with the existing live `vkCreate*`/`vkGet*MemoryRequirements(2)` bodies -- but a targeted CTS run (`dEQP-VK.api.invariance.memory_dedicated_requirements_matching`) found a real gap the row's own premise didn't anticipate: none of the four `vkGet*MemoryRequirements(2)`/`vkGetDevice*MemoryRequirements` entrypoints ever walked their `VkMemoryRequirements2` output's `pNext` chain, so a chained `VkMemoryDedicatedRequirements` disagreed between the live and info-only calls purely because neither touched it. `Memory.h`/`.cpp`'s new `fillMemoryRequirements2PNextChain`, shared by all four, closed it. `maintenance4` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and its own dedicated `VkPhysicalDeviceMaintenance4Features` struct, and `maxBufferSize` (both the aggregate `VkPhysicalDeviceVulkan13Properties` and dedicated `VkPhysicalDeviceMaintenance4Properties` cases) now reads the same real host-memory-size value `VkPhysicalDeviceMaintenance3Properties::maxMemoryAllocationSize` already did, rather than E2's placeholder `0`. See "Roadmap E4: measured impact" in VulkanCTSReport.md for the full before/after) | none | `feme/lib/Vulkan/{Buffer,Image,Memory,EntryPoints}.cpp`, `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P1 |
| E5 | ~~**`VK_KHR_maintenance5`/`maintenance5`.** Chiefly `VkRenderingAttachmentInfo::imageView == VK_NULL_HANDLE` (an attachment slot present but unused, needing `RenderPass.cpp`'s dynamic-rendering path to skip rather than reject a null view), `VK_FORMAT_A8_UNORM`/`A1B5G5R5_UNORM_PACK16`, and `vkCmdBindIndexBuffer2` (a `size`-bounded variant of the existing bind, sharing `CommandBuffer.cpp`'s existing validation minus the "whole buffer" assumption)~~ (closed, with a correction to this row's own file attribution: the null-view skip actually lives in `CommandBuffer.cpp`, not `RenderPass.cpp` -- `normalizeRenderingAttachment` already produced a `RenderTargetView` with a null `View` for a null-imageView slot, but every downstream consumer (the load-op clear, and each draw's per-attachment write and multisample resolve) unconditionally called `resolveAttachmentView` on it and failed; each now skips the slot instead, using the empty-`AttachmentView` "not bound" convention `DepthStencilAttachment` already established, so no live image is required for it. The two new formats slot into the existing `Format.cpp`/`ImageFixture.cpp`/`RenderPass.cpp` format-table machinery exactly like C1's own mandatory formats did, packed the same way `R10G10B10A2_UNORM` already is (one opaque word, special-cased ahead of the generic per-component loop). `vkCmdBindIndexBuffer2` shares `CommandBuffer::bindIndexBuffer`'s recording (`DstSize` reused for its `size`, the same way it already is for `FillBuffer`'s size and `DrawIndirect`'s stride) and `runDraw`/`validateDrawFetchBounds`'s bounds checking, narrowing the readable range to `[offset, offset + size)` instead of assuming "through the end of the buffer" when `size != VK_WHOLE_SIZE`. `maintenance5` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan14Features` struct and its own dedicated `VkPhysicalDeviceMaintenance5FeaturesKHR` struct; none of `VkPhysicalDeviceMaintenance5PropertiesKHR`'s fixed-function rasterizer guarantees are verified yet, so that struct (and the aggregate `VkPhysicalDeviceVulkan14Properties` case's matching fields) stay the conservative `VK_FALSE` E2 established) | E2 | `feme/lib/Vulkan/{RenderPass,CommandBuffer,Format}.cpp` | P1 |
| E6 | ~~**`VK_KHR_maintenance6`/`maintenance6`.** `vkCmdBindDescriptorSets2`/`vkCmdPushConstants2`/`vkCmdPushDescriptorSet2` are shape-compatible wrappers around the existing `Descriptor.cpp`/`CommandBuffer.cpp` entrypoints, taking a `pNext`-extensible info struct instead of a flat argument list; `maxCombinedImageSamplerDescriptorCount`'s reporting is the one new limit (E2 already reserves the field)~~ (closed, with a correction to this row's own scope: `vkCmdPushDescriptorSet2` is deliberately left unimplemented, per this row's own fallback clause -- F12's `pushDescriptor` groundwork has not landed, so `getSupportedDeviceExtensions` advertises `VK_KHR_maintenance6` without it, exactly the same "an extension's optional sub-mechanism can lag its own row" precedent C4c's own 12-of-12 completeness note does not set but F11/F12's own dependency notes anticipate. `vkCmdBindDescriptorSets2`/`vkCmdPushConstants2` share `CommandBuffer::bindDescriptorSets`/`pushConstants`'s existing recording unchanged: neither `VkBindDescriptorSetsInfo`'s `stageFlags` nor `VkPushConstantsInfo`'s `layout`/`stageFlags` needs translation, since this model already stores one shared set of bound descriptor sets/push-constant bytes across every pipeline bind point (see `vkCmdBindDescriptorSets`'s own comment). `maxCombinedImageSamplerDescriptorCount` is a real `1` (this ICD supports no multi-planar/YCbCr samplers, so a combined image sampler descriptor always consumes exactly one slot), in both the aggregate `VkPhysicalDeviceVulkan14Properties` case and a new dedicated `VkPhysicalDeviceMaintenance6Properties` case; `maintenance6` reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan14Features` struct and a new dedicated `VkPhysicalDeviceMaintenance6Features` struct. A targeted CTS run found one real, pre-existing gap this row's own scope does not cover: `dEQP-VK.api.command_buffers.secondary_push_constants_2` fails pipeline creation (`VK_ERROR_INITIALIZATION_FAILED`) on a non-array, single-`vec4`-field `std430` storage buffer block -- the same "resource handle the FeMe CPU target cannot normalize" class of failure that already accounts for 73 of this same CTS group's 77 failures (e.g. every `indirect_compute_dispatch_offsets_*` case), not a regression `vkCmdPushConstants2` itself introduces; see "Roadmap E6: measured impact" in VulkanCTSReport.md) | E2, E12 (push descriptor sets need F12's `pushDescriptor` groundwork first if implemented together, otherwise stub `PushDescriptorSet` count `0`) | `feme/lib/Vulkan/{Descriptor,CommandBuffer}.cpp` | P1 |
| E7 | ~~**`VK_EXT_subgroup_size_control`/`subgroupSizeControl` + `computeFullSubgroups`.** `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` lets a compute pipeline request an explicit subgroup size; `GroupSize.cpp` (already the home of subgroup-size computation, per its name) needs an override path, and `minSubgroupSize`/`maxSubgroupSize`/`maxComputeWorkgroupSubgroups`/`requiredSubgroupSizeStages` (E2's placeholders) become real once this lands~~ (closed, with a correction to this row's own file attribution: the override path actually lives in `Pipeline.cpp`, not `GroupSize.cpp` -- `GroupSize.cpp`'s `resolveComputeGroupSize` resolves a shader's *workgroup* size (`LocalSize`/`LocalSizeId`/`BuiltIn WorkgroupSize`), a different quantity from a dispatch's *subgroup* (wave) size, which this row's own name-pun premise ("`GroupSize.cpp` ... per its name") conflated; no change to `GroupSize.cpp` was needed. `Pipeline.cpp`'s `compileComputePipeline` now reads a chained `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` and forwards it to `feme::cpu::JITOptions::WaveSize` (already validated by `feme::cpu::resolveWaveSize`), and rejects a `VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT` pipeline whose workgroup size in X is not a multiple of the resolved subgroup size; `PipelineCache.cpp`'s cache key now folds in both to avoid a stale hit. `minSubgroupSize`/`maxSubgroupSize`/`maxComputeWorkgroupSubgroups`/`requiredSubgroupSizeStages` are real (`4`/`128`/`32`/`VK_SHADER_STAGE_COMPUTE_BIT`) in both the aggregate `VkPhysicalDeviceVulkan13Properties` case and a new dedicated `VkPhysicalDeviceSubgroupSizeControlProperties` case; a targeted CTS run found 8 pre-existing, out-of-scope `Fail` cases newly reached (a `spirv.SpecConstantComposite` for a shader-body-referenced `BuiltIn WorkgroupSize`, which no conversion pattern lowers) -- see "Roadmap E7: measured impact" in VulkanCTSReport.md for the full before/after) | E2 | `feme/lib/Vulkan/Pipeline.cpp`, `PipelineCache.{h,cpp}`, `EntryPoints.cpp`, `PhysicalDeviceInfo.{h,cpp}` | P1 |
| E8 | ~~**`VK_KHR_shader_integer_dot_product`/`shaderIntegerDotProduct`.** The largest single limit cluster in E2's placeholder set (36 of the 70 fields are `integerDotProduct*Accelerated` bits): decide, measure, and report truthfully whether this CPU target's `OpSDot`/`OpUDot`/`OpSUDot`-family lowering (new `spirv`->`llvm` conversion patterns, since none exist today per `Vulkan14FeatureInventory.md`) is actually hardware-accelerated (likely `VK_FALSE` for all 36 on a CPU executor -- a truthful "supported but not accelerated" is a valid, conformant answer and cheaper than claiming acceleration this target cannot deliver)~~ (closed, exactly as this row's own premise anticipated: `SPIRVToLLVMPatterns.cpp` gained six new patterns -- `spirv.SDot`/`spirv.UDot`/`spirv.SUDot` and their `*AccSat` counterparts, none of which upstream MLIR converts at all, the same "MLIR has no pattern for this op" gap `DotConversionPattern` already closed for the unrelated float `spirv.Dot` -- each lowering to a per-lane sign/zero-extend, multiply, and add chain (a scalar 32-bit operand, legal only with the `PackedVectorFormat4x8Bit` format, is unpacked into its four constituent bytes first), with a final `llvm.intr.sadd.sat`/`uadd.sat` for the `*AccSat` variants. `shaderIntegerDotProduct` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDeviceShaderIntegerDotProductFeatures` struct; `getSupportedDeviceExtensions` gained `VK_KHR_shader_integer_dot_product` itself, needed for the same "CTS enables it by name regardless of `apiVersion`" reason E3/E5/E6 already established (`dEQP-VK.spirv_assembly.instruction.compute`'s own `vktSpvAsmIntegerDotProductTests.cpp` requests it explicitly). This row's own prediction held exactly: all 36 `integerDotProduct*Accelerated` bits stay a truthful `VK_FALSE`, in both the aggregate `VkPhysicalDeviceVulkan13Properties` case and a new dedicated `VkPhysicalDeviceShaderIntegerDotProductProperties` case -- this CPU target runs every one of the six new patterns as an ordinary scalar multiply-add sequence, not a hardware-accelerated one. A targeted CTS run confirms this is a fully conformant answer, not merely a cheaper one: `dEQP-VK.spirv_assembly.instruction.compute`'s six new dot-product groups (1,248 cases within this ICD's advertised operand-width scope) pass 80/0/1,168 (Pass/Fail/NotSupported), zero `Fail`, and `api.info.get_physical_device_properties2.features.shader_integer_dot_product_features` flips from `Fail` (D1/D3's own bucket) to `Pass` -- see "Roadmap E8: measured impact" in VulkanCTSReport.md for the full breakdown) | E2, §1.2 (new `spirv` dialect conversion patterns) | `feme/lib/Conversion/SPIRVToLLVM`, `feme/lib/Vulkan/PhysicalDeviceInfo.cpp` | P1 |
| E9 | ~~**`VK_EXT_pipeline_creation_cache_control`/`pipelineCreationCacheControl`.** `VK_PIPELINE_CREATE_2_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT`/`VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` are flag-only additions to `GraphicsPipeline.cpp`/`Pipeline.cpp`'s existing creation path and `PipelineCache.{h,cpp}`'s existing cache object -- no new object model, purely accepting and honoring two new bits~~ (closed, exactly as this row's own premise anticipated: both bits are flag-only additions, with no new object model. `vkCreateComputePipelines`/`vkCreateGraphicsPipelines` (Pipeline.cpp/GraphicsPipeline.cpp) now check `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT` (the 32-bit `VkPipelineCreateFlags` spelling suffices; no chained `VkPipelineCreateFlags2CreateInfo` is needed for a bit already within the first 32) on a cache miss (or with no cache at all, which can never hit), reporting `VK_PIPELINE_COMPILE_REQUIRED` and leaving that pipeline null instead of compiling for real, without inserting anything into the cache; a more severe result elsewhere in the same batch (a real compile failure, or out-of-host-memory) still wins, per the extension's own spec -- `compileGraphicsPipeline`'s return type changed to `Expected<std::optional<GraphicsPipelineState>>` so a skipped-compile `std::nullopt` is distinguishable from a real `Error` at the call site. `PipelineCache` gained a mutex (and an `ExternallySynchronized` constructor parameter `vkCreatePipelineCache` now threads `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT` into) guarding `lookup`/`insert`/`lookupGraphics`/`insertGraphics` -- the four accessors `vkCreateComputePipelines`/`vkCreateGraphicsPipelines` call -- taken unless that bit is set, since `pipelineCache` is not one of those two commands' own externally-synchronized parameters by default (unlike `vkMergePipelineCaches`'s `dstCache`/`pSrcCaches` and `vkGetPipelineCacheData`'s `pipelineCache`, which always are per the base spec regardless of this extension, so `merge`/`keys` needed no lock at all). `pipelineCreationCacheControl` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDevicePipelineCreationCacheControlFeatures` struct; `getSupportedDeviceExtensions` gained `VK_EXT_pipeline_creation_cache_control` itself, for the same "CTS enables it by name regardless of `apiVersion`" reason E3/E5/E6/E8 already established. See "Roadmap E9: measured impact" in VulkanCTSReport.md for the CTS run) | none | `feme/lib/Vulkan/{GraphicsPipeline,Pipeline,PipelineCache}.{h,cpp}`, `EntryPoints.cpp`, `PhysicalDeviceInfo.cpp` | P2 |
| E10 | ~~**`VK_EXT_private_data`/`privateData`.** `VkPrivateDataSlot` is a new, small object (an opaque per-(object-handle) `uint64_t` map) alongside the existing object model in `Objects.h`; `vkCreatePrivateDataSlot`/`vkSetPrivateData`/`vkGetPrivateData`/`vkDestroyPrivateDataSlot` are new, self-contained entrypoints with no dependency on any other row here~~ (closed, with one file-attribution correction to this row's own premise: `PrivateDataSlot` lives entirely in its own new `PrivateData.{h,cpp}`, not "alongside the existing object model in `Objects.h`" -- it needed no change to `Objects.h` at all, since it never dereferences the `(VkObjectType, uint64_t handle)` pair it is keyed on and so has no dependency on what `Objects.h`'s existing classes represent. All four entrypoints (already core, non-`EXT`-suffixed `VK_VERSION_1_3` names `vk_gen_entrypoints.py`'s `CORE_FEATURES` resolves) landed as designed. `privateData` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDevicePrivateDataFeatures` struct; `getSupportedDeviceExtensions` gained `VK_EXT_private_data` itself, the same "CTS enables it by name regardless of `apiVersion`" reason E3/E5/E6/E8/E9 already established. A targeted CTS run confirms this closes D3's own `api.info.*` bucket for this bit (`private_data_features` flips from `Fail` to `Pass`) and all five `vulkan1p3.*` consistency cases pass, agreeing with the aggregate struct rather than repeating E2's own first-draft regression; `dEQP-VK.api.object_management.private_data.*` passes 37/40, with the 3 non-passes (2 `Fail`, 1 `NotSupported`) all pre-existing, out-of-scope gaps unrelated to this row's own bits (E6/E9's own SIMD-lowering/GEP gap, C3/D3's own divergent-vector "milestone 7 deviation", and an unimplemented `imageCubeArray`, respectively). See "Roadmap E10: measured impact" in VulkanCTSReport.md for the full breakdown) | none | `feme/lib/Vulkan/PrivateData.{h,cpp}`, `EntryPoints.cpp`, `PhysicalDeviceInfo.cpp` | P2 |
| E11 | ~~**`VK_EXT_shader_demote_to_helper_invocation`/`shaderDemoteToHelperInvocation`.** SPIR-V's `OpDemoteToHelperInvocation` needs a new `spirv`->`llvm` conversion pattern (mark the invocation inactive for further side effects, matching HLSL `discard`'s existing non-terminating semantics rather than DXIL's `discard`'s current lowering, if one exists -- audit `SPIRVToLLVMPatterns.cpp` first)~~ (closed, with one premise correction: the audit found no existing `spirv`->`llvm` pattern for *either* `OpKill` or `OpDemoteToHelperInvocation` at all -- and MLIR's own upstream SPIR-V dialect had no op at all for `OpDemoteToHelperInvocation`, despite already having its `Capability`/`Extension` enum cases, so `mlir::spirv::deserialize` would have rejected any real module using it. This row therefore added `spirv.DemoteToHelperInvocation` to MLIR itself (a non-terminator, unlike the deprecated `spirv.Kill`) alongside a new `llvm.spv.demote.to.helper.invocation` LLVM intrinsic (mirroring `llvm.spv.discard`'s shape, but kept distinct since this ICD's own import direction needs to preserve the terminating-vs-non-terminating distinction the emit-direction backend collapses when selecting between `OpKill`/`OpDemoteToHelperInvocation`). `SPIRVToLLVMPatterns.cpp`'s new `DemoteToHelperInvocationConversionPattern` converts the op to that intrinsic; `CanonicalizeStage.cpp` raises it into `feme.stage.demote(true)` -- unconditional, matching `llvm.spv.discard`'s own existing raising -- whose reference/SIMD lowering (`StageOpKind::Demote`) already existed. `shaderDemoteToHelperInvocation` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures` struct; `getSupportedDeviceExtensions` gained `VK_EXT_shader_demote_to_helper_invocation` itself, the same "CTS enables it by name regardless of `apiVersion`" reason E3/E5/E6/E8/E9/E10 already established. `OpTerminateInvocation` (roadmap E12) remains a separate, unimplemented op. See "Roadmap E11: measured impact" in VulkanCTSReport.md for the CTS run) | §1.2 | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P1 |
| E12 | ~~**`VK_KHR_shader_terminate_invocation`/`shaderTerminateInvocation`.** SPIR-V's `OpTerminateInvocation` (a true terminator, unlike `OpDemoteToHelperInvocation`) needs its own conversion pattern lowering to an unconditional discard-and-return. **Closes D3's `graphicsfuzz` 72-case regression** (CTS assumes this promoted-to-1.3 extension is real once `apiVersion >= 1.3`)~~ (closed, exactly as this row's own premise anticipated -- with one premise correction the audit found, the same shape E11's own audit found: no `spirv`->`llvm` conversion pattern existed for `OpTerminateInvocation` at all, and MLIR's own upstream SPIR-V dialect had no op for it either, so a new `spirv.TerminateInvocation` op (a true terminator, requiring no capability beyond the existing `Shader` one) had to be added to MLIR itself first. `SPIRVToLLVMPatterns.cpp`'s new `TerminateInvocationConversionPattern` then converts it into exactly the unconditional discard-and-return this row's own premise specified: a call to the same `llvm.spv.discard` intrinsic `OpKill` itself would use (already raised into `feme.stage.discard(true)` by the existing `CanonicalizeStagePass` renaming, unmodified by this milestone), followed by an `llvm.return`. `shaderTerminateInvocation` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDeviceShaderTerminateInvocationFeatures` struct; `getSupportedDeviceExtensions` gained `VK_KHR_shader_terminate_invocation` itself, the same "CTS enables it by name regardless of `apiVersion`" reason E3/E5/E6/E8/E9/E10/E11 already established. A targeted CTS run found a *second* premise correction, to the `graphicsfuzz` row's own D3-era characterization rather than this row's: all 72 `OpTerminateInvocation`-using `graphicsfuzz` cases fail identically at Amber's own pre-flight color-attachment-format check, before any pipeline is created or shader executed -- the same pre-existing, already-documented `vkGetPhysicalDeviceFormatProperties` stub gap that blocks the entire 757-case `graphicsfuzz` group uniformly (confirmed against a 20-case control sample unrelated to this extension), not a wrong-image mismatch, and out of this row's own scope to fix; see "Roadmap E12: measured impact" in VulkanCTSReport.md) | §1.2 | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P0 |
| E13 | ~~**`VK_KHR_zero_initialize_workgroup_memory`/`shaderZeroInitializeWorkgroupMemory`.** `Workgroup`-storage-class SPIR-V globals need a zero-initializer emitted once per dispatch (likely in `feme::cpu::SPIRVResourceLoweringPass` or a small new pass run before it) rather than reading whatever the host's memory allocator happened to leave behind. **Closes D3's `compute.pipeline.zero_initialize_workgroup_memory` 7-case regression**~~ (closed, with a premise correction the audit found, a different shape than E11/E12's own missing-conversion-pattern one: `Workgroup`-storage-class `spirv.GlobalVariable`s had no conversion pattern at all -- MLIR's own upstream `GlobalVariablePattern` supports only `Input`/`Private`/`Output`/`StorageBuffer`/`UniformConstant` -- so `feme::spirv::WorkgroupGlobalVariablePattern` (SPIRVToLLVMPatterns.cpp) had to be added first, converting one to an ordinary `llvm.mlir.global` in address space 3 (the same convention Clang's own HLSL `groupshared` codegen already uses), alongside a new pointer-type conversion routing an access chain through the same address space. Importing the zero-initializer itself needed two further MLIR-level fixes: `spirv.GlobalVariable` gained `zero_initialized`, a new unit attribute representing the one further shape SPIR-V's `Initializer` operand permits (a plain `OpConstantNull`, which has no symbol of its own to reference, unlike the existing `initializer` symbol-reference case), and `processConstantNull` (the deserializer) was generalized to build a null value for a composite (`spirv.array`/`spirv.struct`) type too, not just a scalar/vector/tensor one -- both real, independent gaps, not specific to this row's own feature. `feme::cpu::GroupSharedLayout` gained `NeedsZeroInit` (GroupShared.h), read off the imported global's own `hasInitializer()`; `feme::cpu::EntryWrapperPass` `memset`s the whole flat groupshared buffer to zero, once per group, when it is set (EntryWrapper.cpp) -- deliberately over-broad (zeroing every groupshared global in the module, not just the flagged one) but still correct, since a groupshared global with no zero-initializer of its own was already free to read as anything. `shaderZeroInitializeWorkgroupMemory` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures` struct; `getSupportedDeviceExtensions` gained `VK_KHR_zero_initialize_workgroup_memory` itself, the same "CTS enables it by name regardless of `apiVersion`" reason E3/E5/E6/E8/E9/E10/E11/E12 already established. A targeted CTS run found this row's own "Closes ... 7-case regression" text undercounts both directions: the real `compute.pipeline.zero_initialize_workgroup_memory` group is 644 cases, of which 4 (`types.{bool,float32_t,int32_t,uint32_t}`, the exact scalar zero-init shape this row's own scope targets) now genuinely `Pass`, while the remainder stay `Fail`/`NotSupported` behind two further pre-existing, out-of-scope gaps this row's own audit found but does not fix: `OpTypeArray`'s Length operand accepting only a normal constant, not the specialization constant every non-scalar case in this CTS source sizes its array from; and a vector/matrix `shared` variable's own element access producing a vector (not scalar) GEP base LLVM's translation rejects. See "Roadmap E13: measured impact" in VulkanCTSReport.md for the full per-subgroup breakdown, including a third, genuine robustness gap (an `i1`-composite GEP crashing `mlir::translateModuleToLLVMIR` outright) found and isolated but left unfixed, same as the other two) | §1.2/§1.6 | `feme/lib/Target/CPU` | P0 |
| E14 | ~~**`VK_EXT_inline_uniform_block`/`inlineUniformBlock` + `descriptorBindingInlineUniformBlockUpdateAfterBind`.** A new `VkDescriptorType` whose "descriptor" is inline byte storage rather than a handle -- `Descriptor.{h,cpp}`'s existing per-binding storage needs a byte-blob variant alongside its current handle-array one, and `VkWriteDescriptorSetInlineUniformBlock` is a new `pNext` case in the existing `vkUpdateDescriptorSets` walk~~ (closed, exactly as this row's own premise anticipated -- with one further mechanical extension the premise did not spell out: `vkUpdateDescriptorSetWithTemplate` and `VkCopyDescriptorSet`'s own copy path (both in Descriptor.cpp, sharing the same object model) needed the identical byte-ranged special case as `vkUpdateDescriptorSets` itself, since all three read `dstArrayElement`/`srcArrayElement`/`descriptorCount` as byte offsets/counts rather than array indices/counts for this one descriptor type. `DescriptorSet` gained `InlineUniformBlockBindings` (a `std::map<uint32_t, std::vector<uint8_t>>`), sized from the layout binding's own `descriptorCount` reinterpreted as a byte size per spec. `inlineUniformBlock` now reads `VK_TRUE` from both the aggregate `VkPhysicalDeviceVulkan13Features` struct and a new dedicated `VkPhysicalDeviceInlineUniformBlockFeatures` struct, alongside real (spec-minimum-floor) `VkPhysicalDeviceInlineUniformBlockProperties` limits agreeing with the aggregate `VkPhysicalDeviceVulkan13Properties` case; `descriptorBindingInlineUniformBlockUpdateAfterBind` stays `VK_FALSE` -- no update-after-bind/descriptor-indexing mechanism exists anywhere in this ICD yet, so there is no honest "yes" to give that bit independent of the rest of that extension family. `getSupportedDeviceExtensions` gained `VK_EXT_inline_uniform_block` itself. This milestone's own scope is the descriptor object model only, per its "closes: none": no `feme::cpu::SPIRVResourceLoweringPass` conversion consumes an inline uniform block from a real dispatch yet, the same "object model first, shader consumption later" shape V5's image/sampler descriptor types already established (`FeMeVulkanDesign.md`'s Descriptor Model table updated to match). A targeted CTS run found a genuine, in-scope limits bug this row's own premise did not anticipate: `dEQP-VK.api.info.vulkan1p2_limits_validation.ext_inline_uniform_block` requires `maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks`/`maxDescriptorSetUpdateAfterBindInlineUniformBlocks` to meet the same `>= 4` floor as their non-`UpdateAfterBind` counterparts *unconditionally*, independent of `descriptorBindingInlineUniformBlockUpdateAfterBind`'s own value -- unlike Vulkan 1.2's own descriptor-indexing `UpdateAfterBind` limits, which stay `0` uncontested alongside a `VK_FALSE` `descriptorIndexing`; fixed by reporting both equal to their non-`UpdateAfterBind` counterparts. Of the 136 `*inline_uniform_block*`-named cases run, 3 genuinely `Pass` (the feature/property advertisement itself), 17 fail at graphics pipeline creation (this ICD is compute-only, unrelated to this row), and 8 (`descriptor_copy.compute.inline_uniform_block_*`) fail cleanly at compute pipeline creation -- a real compute shader consuming the binding, exactly the deferred-dispatch-consumption scope this row's own text states. See "Roadmap E14: measured impact" in VulkanCTSReport.md for the full breakdown, including a full `dEQP-VK.api.*`/`dEQP-VK.binding_model.*` regression check finding no crash or new failure from advertising this extension) | none | `feme/lib/Vulkan/Descriptor.{h,cpp}` | P1 |
| E15 | **`VK_EXT_texture_compression_astc_hdr`/`textureCompressionASTC_HDR`.** A new decode path in `Format.{h,cpp}` for the 14 ASTC HDR block formats, reusing whatever LDR ASTC decode already exists (check `Format.cpp` first — if LDR ASTC is itself unimplemented, this row's scope grows to include it and should be split) | none (verify LDR ASTC status first) | `feme/lib/Vulkan/Format.{h,cpp}` | P2 |
| E16 | **`VK_EXT_image_robustness`/`robustImageAccess`.** Out-of-bounds image reads/writes must return/discard rather than fault; audit `Image.cpp`'s/`ImageOps.cpp`'s existing bounds handling (Executor's texel read/write path) and add an explicit clamp-or-discard for any coordinate outside the image's declared extent that isn't already handled | none | `feme/lib/Vulkan/{Image,ImageOps}.cpp` | P1 |
| E17 | **SPIR-V 1.6 `Nontemporal` image-operand bit.** Not a feature-bit gap but a shader-compilation one: `ImageFetchPattern`/`ImageFetchLodPattern`/`ImageSampleExplicitLodPattern` (`SPIRVToLLVMPatterns.cpp`) reject any image operand mask they don't recognize exactly, and SPIR-V 1.6's cache hint bit has no case. Since it has no correctness effect, the fix is to accept and discard it rather than model caching. **Closes D3's `spirv_assembly.instruction.compute` 422-case regression**, the largest single item in this section | none | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P0 |
| E18 | **Texel-buffer-format/robustness inconsistency** (D3's `robustness.oob_access` 6-case regression, not yet root-caused past "format/robustness mismatch" -- this row starts with tracing it to a specific line, the way D3 did for its other five buckets, before fixing it) plus `VK_EXT_texel_buffer_alignment`'s two limit fields (`storageTexelBufferOffsetAlignmentBytes`/`SingleTexelAlignment`, `uniformTexelBufferOffsetAlignmentBytes`/`SingleTexelAlignment`, already reserved by E2) | E2 | `feme/lib/Vulkan/{Format,Buffer}.cpp` | P0 |
| E19 | **Remaining small 1.3 extensions with no feature bit of their own**, each a mechanical, independent addition: `VK_EXT_4444_formats` (2 new `Format.cpp` formats), `VK_EXT_ycbcr_2plane_444_formats` (1 new format, decline if YCbCr sampling itself is unimplemented — check first), `VK_EXT_pipeline_creation_feedback` (`VkPipelineCreationFeedback` timing struct, can honestly report zero/estimated timings), `VK_KHR_format_feature_flags2` (`VkFormatProperties3`, mirrors the existing `VkFormatProperties2` case with the 2 new bit ranges), `VK_KHR_shader_non_semantic_info` (SPIR-V debug-info-only opcodes the importer can already skip/ignore), `VK_EXT_tooling_info` (`vkGetPhysicalDeviceToolProperties` can truthfully report zero tools) | none | `feme/lib/Vulkan/{Format,Pipeline,EntryPoints}.cpp` | P2 |

Recommended independent lanes for parallel distribution (each lane's rows
share files/reviewers; lanes themselves are independent once E1/E2 land):
**Lane 1** (sync/dispatch): E3, E6. **Lane 2** (memory/image queries): E4,
E5, E16, E18. **Lane 3** (compute/shader): E7, E8, E11, E12, E13 (E12/E13
are P0 and should not wait for E7/E8/E11 in the same lane). **Lane 4**
(descriptors/misc small extensions): E9, E10, E14, E15, E19. **Lane 5**
(shader compilation, no feature-bit dependency, start immediately): E17.

#### 1.9.5 Closing the Vulkan 1.4 mandatory floor (F-series)

Same granularity and testing discipline as §1.9.4. Every row here depends
on E1/E2's struct plumbing (the 1.4 aggregate cases, not just the 1.3
ones) but not on any specific E-row otherwise, so — per §1.9.3's Strategy
B — this whole section can start as soon as E1/E2 land if enough
independent capacity exists, even before §1.9.4 fully closes.

| # | Task | Depends on | Files | Priority |
|---|---|---|---|---|
| F1 | **`VK_KHR_global_priority`/`globalPriorityQuery`.** `VkDeviceQueueGlobalPriorityCreateInfo` at `vkCreateDevice` and `vkGetPhysicalDeviceQueueFamilyProperties2`'s `VkQueueFamilyGlobalPriorityProperties` chain; since this ICD has one worker pool with no real OS-level scheduling priority, report the full mandatory priority list as supported and treat the create-time hint as a no-op (matching the "single logical queue, narrowed by capability flags only" precedent C7 set) | E1/E2 | `feme/lib/Vulkan/PhysicalDeviceInfo.cpp` | P2 |
| F2 | **`VK_KHR_shader_subgroup_rotate`/`shaderSubgroupRotate`(+`Clustered`).** New `spirv`->`llvm` conversion patterns for `OpGroupNonUniformRotateKHR`; note `Vulkan14FeatureInventory.md`'s existing finding that no `OpGroupNonUniform*` operation converts at all yet (`shaderSubgroupExtendedTypes` is vacuously true for the same reason) — this row should audit and likely close that whole family together rather than add one more vacuous bit | §1.2 | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P1 |
| F3 | **`VK_KHR_shader_float_controls2`/`shaderFloatControls2`.** Per-instruction (rather than per-module) rounding-mode/denorm-preservation execution modes; audit whether `VK_KHR_shader_float_controls`'s per-module form is implemented at all first (`Vulkan14FeatureInventory.md` doesn't list it, so check `SPIRVToLLVMPatterns.cpp`/`ImportSPIRV` before scoping this row) | §1.2 | `feme/lib/Conversion/SPIRVToLLVM` | P2 |
| F4 | **`VK_KHR_shader_expect_assume`/`shaderExpectAssume`.** `OpAssumeTrueKHR`/`OpExpectKHR` lower directly to `llvm.assume`/`llvm.expect` intrinsics — one of the smallest rows in this whole breakdown, good as a first task for a new contributor | §1.2 | `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` | P2 |
| F5 | **`VK_KHR_line_rasterization`** (`rectangularLines`/`bresenhamLines`/`smoothLines` + their `stippled*` variants) **+ its 2 limit fields.** C4d already built a line-topology rasterizer (1-pixel-wide quad expansion); this row generalizes it to variable width and the three line styles, then adds stippling as a per-fragment pattern test against `VkPipelineRasterizationLineStateCreateInfo`'s stipple factor/pattern. The largest single G-track item in this section — consider splitting rectangular/bresenham/smooth from the three `stippled*` variants into two separately assignable pieces | E2, §1.8 (rasterizer) | `feme/lib/Vulkan/GraphicsPipeline.cpp`, `feme::graphics` executor | P1 |
| F6 | **`VK_KHR_vertex_attribute_divisor`/`vertexAttributeInstanceRateDivisor`(+`Zero`) + `maxVertexAttribDivisor`.** `VkVertexInputBindingDivisorDescription` extends the existing per-binding instance-rate stepping already implied by instanced draws; a divisor of 0 (every instance reads vertex 0) is the one new case, not a new mechanism | E2 | `feme/lib/Vulkan/GraphicsPipeline.cpp` | P2 |
| F7 | **`VK_KHR_index_type_uint8`/`indexTypeUint8`.** `vkCmdBindIndexBuffer`'s existing 16/32-bit index read in `CommandBuffer.cpp`/the executor gains an 8-bit case — mechanical, narrow, good second task after F4 | none | `feme/lib/Vulkan/CommandBuffer.cpp`, `feme::graphics` executor | P2 |
| F8 | **`VK_KHR_dynamic_rendering_local_read`/`dynamicRenderingLocalRead` + its 2 limit fields.** `vkCmdSetRenderingAttachmentLocations`/`vkCmdSetRenderingInputAttachmentIndices` let a fragment shader read the current attachment bindings as input attachments without a render-pass restart; builds on V6's dynamic-rendering render-target binding | E2, V6 (done) | `feme/lib/Vulkan/{RenderPass,GraphicsPipeline}.cpp` | P1 |
| F9 | **`VK_EXT_pipeline_protected_access`/`pipelineProtectedAccess`.** This ICD has no protected-memory model at all (no protected queue/allocation exists); the honest, conformant answer is likely to accept the flag as a no-op restriction (reject creating a protected+unprotected-mixed pipeline, matching the spec's validation rules) without implementing real memory protection — confirm against the spec's exact conformance requirement before assuming this is sufficient | none | `feme/lib/Vulkan/{GraphicsPipeline,Pipeline}.cpp` | P2 |
| F10 | **`VK_EXT_pipeline_robustness`/`pipelineRobustness` + its 4 `defaultRobustness*` limit fields.** `VkPipelineRobustnessCreateInfo` lets a pipeline opt in/out of robust buffer/image access per-binding-class rather than only device-wide; depends on E16's image-robustness groundwork existing to have something to opt in/out of | E2, E16 | `feme/lib/Vulkan/{GraphicsPipeline,Pipeline}.cpp` | P1 |
| F11 | **`VK_EXT_host_image_copy`/`hostImageCopy` + its 6 limit/list fields.** The largest single new mechanism in this section: `vkCopyMemoryToImage`/`vkCopyImageToMemory`/`vkCopyImageToImage`/`vkTransitionImageLayout` copy/transition without a command buffer at all, needing a host-side (not executor-queued) path into `Image.cpp`'s existing layout/format machinery; `pCopySrcLayouts`/`pCopyDstLayouts` (per D1's own finding, an enumerated list, not a scalar limit) is the supported-layout list this new path accepts | E2, Image.cpp | new `feme/lib/Vulkan/HostImageCopy.{h,cpp}` | P1 |
| F12 | **`VK_KHR_push_descriptor`/`pushDescriptor` + `maxPushDescriptors`.** `vkCmdPushDescriptorSet` writes descriptors directly into a command buffer's recorded state without a `VkDescriptorSet` object at all — a new, lighter-weight descriptor path alongside `Descriptor.{h,cpp}`'s existing pool-backed one, sharing its binding-to-heap-slot translation | E2, E6 (F12 and E6's `vkCmdPushDescriptorSet2` share the same underlying mechanism — implement together or explicitly sequence one before the other) | `feme/lib/Vulkan/{Descriptor,CommandBuffer}.cpp` | P1 |
| F13 | **`VK_KHR_load_store_op_none` (no feature bit).** `VK_ATTACHMENT_LOAD_OP_NONE`/`VK_ATTACHMENT_STORE_OP_NONE` are two new enumerants `RenderPass.cpp`'s existing load/store-op switch already has a natural "do nothing" case for — one of the smallest rows in this whole breakdown | none | `feme/lib/Vulkan/RenderPass.cpp` | P2 |
| F14 | **`VK_KHR_map_memory2`.** `vkMapMemory2`/`vkUnmapMemory2` are `pNext`-extensible wrappers around the existing `Memory.cpp` map/unmap, adding `VkMemoryUnmapFlagsKHR`'s reserve-on-unmap bit | none | `feme/lib/Vulkan/Memory.cpp` | P2 |

Recommended independent lanes: **Lane 1** (shader compilation, no
feature-bit dependency once E1/E2 land): F2, F3, F4. **Lane 2**
(rasterizer/graphics pipeline): F5, F6, F13. **Lane 3** (command
buffer/index/memory): F7, F14. **Lane 4** (descriptors, sequence F12 and
E6 together): F12. **Lane 5** (robustness/protected access, sequence
after E16): F9, F10. **Lane 6** (new subsystems, largest single items,
good candidates for a dedicated owner rather than a quick task): F1, F8,
F11.

#### 1.9.6 Cross-cutting tasks, not gated on any single E/F row

| # | Task | Priority |
|---|---|---|
| G1 | **Promote D4 (continuous, crash-tolerant CTS measurement) from "whenever time allows" to a hard prerequisite for merging any E/F row past the first two or three.** With ~33 independent rows landing from potentially-independent agents, a hand-rolled per-run Python diff (D0-D3's own method) does not scale; land D4 early enough that each E/F row's own PR can cite an automated before/after rather than a manually-run one | P0 |
| G2 | **Finish roadmap C10** (a checked-in expected-failure list and full 54-group CI job), still open per §1.9.1's table. Without it, an E/F row's regression is discovered only by whoever happens to run the full suite next, the same class of gap D0's own second-pass loader crash and D3's misattributed buckets both trace back to | P0 |
| G3 | **A per-row CTS re-measurement discipline**, matching D1-D3's own precedent: every E/F row's PR should cite the specific `dEQP-VK` group(s) it targets and the actual before/after count for that group — not the whole 54-group suite unless the change plausibly touches more than one (the same "targeted, real, not simulated" scoping D1 used for its own no-op entrypoint-table change) | P0 |
| G4 | **Re-triage `Vulkan14FeatureInventory.md`'s two verify-first rows** (E15's ASTC HDR depends on LDR ASTC's own status, F3's `shaderFloatControls2` depends on the unpromoted `shaderFloatControls`'s own status) before assigning them, since each may expand into its own separate row once checked | P1 |
| G5 | **Once §1.9.4 (1.3) is fully closed under Strategy A, or continuously under Strategy B, re-run and update this section's own "not yet measured" rows** with real CTS deltas, the same way §1.9.1's C-rows each grew a "measured impact" parenthetical as they closed | P1 |

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
| R30 | `feme.image.*`/`feme.sampler.*` canonicalization from DXIL (including §1.3's texture/sampler handle-kind gap) and SPIR-V (including §1.2's sampling variants), the `runtime/CPU` sampling helpers (1D/2D addressing, mip layout, point/linear filtering, explicit and implicit LOD, addressing modes, comparison sampling), the initial format table with sRGB, and active-lane SIMD lowering. **Completes G2**, unblocking V5 and W3 (status: 2D sampling/loading landed end to end -- DXIL `Sample`/`SampleLevel`/`TextureLoad`/`GetDimensions.x`/`.xy` raising, SPIR-V `ImageSampleExplicitLod` conversion, `runtime/CPU`'s addressing/filtering/mip-selection/comparison-sampling/format-table helpers, and `feme::cpu::ResourceLoweringPass`'s consumption of both into `feme.cpu.image.*`, wired through `EntryWrapperPass`/`ReferenceEntryWrapperPass`/`VertexWrapperPass`/`FragmentWrapperPass` -- and, in a follow-up pass, **G2 is now complete for the 2D sampled-image path end to end**: `feme::cpu::SPIRVResourceLoweringPass` normalizes a bound 2D `spirv.Image`/`spirv.Sampler` pair into the image and sampler heaps and lowers `llvm.spv.resource.sample`/`samplelevel`/`OpImageFetch` into the same `feme.cpu.image.*` calls, `feme::cpu::BoundResourceRange` gained a `BoundResourceClass` (plus `ReservedImageHeapSize`/`ReservedSamplerHeapSize`, artifact ABI version 5) so a host can place a bound image/sampler descriptor at all, `feme::cpu::materializeImageHeap`/`materializeSamplerHeap` do that placement, and `feme::cpu::SIMDize`'s new `widenImageCall` scalarizes a *divergent* sample per lane. That unblocked V5 -- see its own status note. Still open, each documented in FeMeGraphicsDesign.md's "Canonical image operations"/"Texture layout and formats" status notes: 1D/3D/cube sampling, arrayed/multisampled/storage images, bias/gradient sampling, comparison sampling on DXIL (blocked upstream -- no numbered DXIL wire opcode exists in this LLVM tree to raise from), gather, and nonzero texel offsets) | G2 | §1.8.4, §1.2, §1.3 | R29 |
| R31 | `FeMeGraphics` skeleton: normalized pipeline and prepared-draw descriptions, the `feme-render` tool (already specified in Design.md's "Testing Tools" and `docs/CommandGuide/feme-render.md`, along with its scene and image fixture formats -- only the implementation is left), and the heap YAML image resource class (done: new `FeMeGraphics` library (`feme/include/feme/Graphics`, `feme/lib/Graphics`) defines `feme::graphics::GraphicsPipeline`/`PreparedDraw` as plain description types -- the former owns the compiled vertex/fragment `feme::cpu::CompiledStage`s plus primitive topology, raster/depth/blend state and attachment formats, the latter holds one draw's attachments, viewport/scissor, vertex buffers, resource heap and draw commands -- matching "Normalized pipeline" in FeMeGraphicsDesign.md, but implementing no clip/raster/interpolation logic (that is R32). The same library implements the textual image fixture (`feme::graphics::ImageFixture`) and scene (`feme::graphics::Scene`) formats "Textual scene and image fixtures" in Design.md specifies, shared by the new `feme-render` tool and `unittests/Graphics/` as that section requires; fixture format coverage matches what `runtime/CPU`'s image helpers already implement (`R8G8B8A8_*` and the `R32*_FLOAT/UINT/SINT` family) and grows mechanically on demand. `feme-render` (`feme/tools/feme-render`) implements the CLI docs/CommandGuide/feme-render.md already specified: it parses a scene, builds and clears every attachment, compiles `pipeline.vertex`/`pipeline.fragment` into a real `GraphicsPipeline` when a scene has one, and dumps attachments (`--dump`, `--expect`, `--tolerance`); a non-empty scene `draws` list is diagnosed as not implemented rather than silently misrendering, since R32 is what actually executes one. `feme-run`'s heap YAML gains an `images` list (`ImageEntry` in feme-run.cpp), building `feme::cpu::FemeImageDescriptor`s into the ABI's separate image heap alongside `resource-heap`/`bindings`, covering a single mip level and (for a non-array dimension) a single array layer; multisample dimensions are rejected, matching G4's later multisample milestone. Shader modules `feme-render` loads are plain already-raised LLVM IR only for now -- DXIL/SPIR-V import follows `feme-run`'s own precedent once a test needs it. `test/Tools/feme-run/heap-image*.ll`, `unittests/Graphics/{ImageFixture,Pipeline,PreparedDraw,Scene}Test.cpp`, and `test/Tools/feme-render/*.test` cover this row; `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G3 | §2.6.1 | R28 |
| R32 | Vertex/index fetch, triangle assembly, clipping, viewport transform, culling, tile binning, top-left coverage, interpolation, and both stages run through the executor: one color attachment, one viewport/scissor, no MSAA. **Completes G3** (done: `feme::graphics::executeDraws` (`feme/include/feme/Graphics/Executor.h`, `feme/lib/Graphics/Executor.cpp`) implements the "Draw flow" FeMeGraphicsDesign.md specifies end to end for one `TriangleList`/`TriangleStrip` draw against one color attachment: vertex/index fetch decodes bound vertex-buffer attributes (the 32-bit float/int family and `R8G8B8A8_*`) matched to a vertex-stage input by `Location`; vertex-output/fragment-input varyings are linked by `Location` (Vulkan-style, since no `StageInterfaceMap` exists yet); triangles are clipped against all 6 homogeneous half-spaces (plus a `w > 0` guard) via Sutherland-Hodgman and fan-triangulated; viewport transform, front-face culling and top-left-rule coverage share one self-consistent directed-edge-function convention; primitives are binned into fixed-size tiles, each batching its own covered 2x2 quads (with helper lanes) into one `invokeFragments` call, with output merge performed in submission order (painter's algorithm, since depth testing is R33's); interpolation is perspective-correct/no-perspective/flat per `SignatureInterpolationMode`. `PreparedDraw` (R31) grew `VertexBufferBinding::Attributes` and `IndexBufferBinding`/`DrawCommand::Indexed` for index buffers; the scene YAML (`feme::graphics::Scene`) grew a matching `index-buffer` key and per-draw `indexed`/`first-index`/`vertex-offset` fields. `feme-render` now executes a scene's `draws` instead of diagnosing them as unimplemented, encoding `vertex-buffers`/`index-buffer` scene data into the executor's byte layouts and defaulting `viewport`/`scissor` to the sole color attachment's extent when the scene omits them. Deferred, each a documented scope note in Executor.cpp's own file comment: no post-transform vertex cache (every vertex re-runs, matching "the first implementation may perform all vertex work before tile work"); matrices/16-/64-bit stage elements; a non-`Float` varying is carried from the first vertex of the *rasterized* (possibly clipped) triangle rather than tracking the original mesh's provoking vertex through clipping; and `--workers`/`--tile-order`/`--reference` remain accepted-but-inert in `feme-render` (the executor is already a deterministic single-threaded scalar implementation, so every value of each produces identical output, satisfying but not yet exercising the metamorphic checks "Determinism and Reference Execution" describes -- true parallel tiling and a differential scalar-reference path are scheduling optimizations, not part of this milestone's correctness scope). `unittests/Graphics/ExecutorTest.cpp` covers full/partial coverage, indexed draws, back-face culling, unsupported-topology rejection, perspective-correct interpolation, and the top-left tie-break's gap/overlap-free adjacent-triangle property; `test/Tools/feme-render/draw-{triangle,vertex-buffer,indexed}.test` cover the CLI path. `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G3 | §1.8.5 | R31, R30 |
| R33 | Depth/stencil attachments with legal early/late scheduling, blending, write masks, logic ops, multiple render targets, multisample coverage and resolves, the format expansion the first advertised profile needs, and deterministic parallel tiled schedules (done: `feme::graphics::executeDraws` implements every bullet against `feme::graphics::GraphicsPipeline`'s new `StencilState`/`BlendState`/`LogicOp`/`getColorBlends()` state and `PreparedDraw`'s new `DepthStencilAttachment`/`ResolveAttachments`. Depth (`D16_UNORM`/`D32_FLOAT`) and stencil (`S8_UINT`) were originally two separate attachments rather than one packed surface; roadmap C1 ("Mandatory formats") added real pack/unpack support for the combined `D24_UNORM_S8_UINT` format (`feme::graphics::packDepthClear`/`packStencilClear` and their `unpack*` inverses are independent read-modify-writes of the shared word's two halves), so a subpass may now bind either or both halves of that format to the same image; `D32_FLOAT_S8X24_UINT` remains declared in `cpu::ResourceFormat` but not yet wired, a mechanical, on-demand addition matching this codebase's established "declared, not yet wired" convention. Early-vs-late depth/stencil scheduling is chosen from the fragment stage's own already-existing reflection (`SignatureSystemValue::Depth`/`StencilRef` outputs, `FEME_CPU_ARTIFACT_USES_DISCARD`/`_DEMOTE`) -- no new reflection pass was needed, since the compute track's discard/demote lowering and the graphics signature model already carried everything "Early and late tests" in FeMeGraphicsDesign.md needs. Blending implements the full Vulkan/Direct3D-shared `BlendFactor`/`BlendOp` equation plus a per-channel write mask honored regardless of blend/logic-op state; logic ops are implemented for `R8G8B8A8_UNORM/_UINT/_SINT` only, matching both APIs' own restriction on which formats support one. Multiple render targets link each fragment `SV_TargetN` output to `Draw.Attachments[N]` with its own `BlendState`. Multisampling supports 1/2/4 samples at fixed, deterministic per-pixel sample offsets (FeMe's own convention, not copied from either API's standard pattern, per "Determinism and Reference Execution"'s fixed-sample-location requirement); coverage is tested per sample (`FemeFragmentInvocation::Coverage`'s long-documented "coverage mask" meaning, previously always 0/1) and depth/stencil tested/written per sample against its own stored value, but shading and the depth/stencil candidate value stay per-pixel -- a documented precision scope decision, not a gap in the coverage/resolve correctness a completion test observes. Depth/stencil resolve and 8+ sample counts are mechanical follow-ups. Deterministic parallel tile scheduling dispatches `processTile` across `WorkerCount` worker threads pulling from a shared atomic cursor; since tiles own disjoint attachment regions, output is bit-identical regardless of worker count or tile order, wiring up `feme-render`'s previously-inert `--workers` flag. `feme::graphics::Scene`/`feme-render` grow `depth-attachment`/`stencil-attachment` scene keys (documented in Design.md); MRT/blend/stencil/MSAA scene YAML wiring beyond the depth attachment is not yet added to `feme-render` itself -- a mechanical follow-up, since the executor library is what this row's completion test exercises directly. `unittests/Graphics/ExecutorTest.cpp` covers depth test/write/early-late scheduling, stencil test/ops, blending, write masks, logic ops, MRT, multisample coverage/resolve, sample-count rejection, and the worker-count determinism property; `unittests/Graphics/ImageFixtureTest.cpp` covers the new depth/stencil fixture formats and `unpackColor`; `test/Tools/feme-render/draw-depth.test` covers the depth attachment end to end. `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after) | G4 | §1.8.5, §2.6.3 | R32 |
| R34 | Geometry/hull/domain signatures and wrappers, patch storage, control-stage barriers, tessellator state and domain-coordinate generation, bounded geometry streams, stream output, adjacency, layered rendering (status: the host-side, standalone-tested core lands -- the signature/stage-op model (`SignatureSystemValue::TessFactorEdge`/`TessFactorInside`/`DomainLocation`/`OutputControlPointID`, `StageOpKind::StreamEmit`/`StreamCut`, patch input/output reusing the existing `InputLoad`/`OutputStore` ops), the fixed-function tessellator (`feme::graphics::tessellate`, new Tessellator.h, generating domain coordinates/connectivity for isoline/triangle/quad domains across every partitioning/output-primitive combination, including crack-free non-uniform per-edge tessellation for the triangle/quad domains -- each edge's own factor places that edge's boundary vertices, so two adjacent patches agreeing on a shared edge's factor produce identical vertices along it regardless of their other factors, bridged to a uniformly-subdivided interior core via a concentric-ring triangulation, `bridgeRingsByEdge`), bounded patch storage (`feme::graphics::PatchRecord`, new Patch.h -- control-stage barriers need no new code, since `feme::cpu`'s groupshared/barrier lowering is already stage-agnostic), the four adjacency `PrimitiveTopology` variants plus list- and strip-topology adjacency splitting (Pipeline.h's `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency`), a bounded per-invocation multi-stream geometry builder (`feme::graphics::GeometryStreamBuilder`, new GeometryStream.h) retaining strip boundaries/emission order for stream output and rasterization to share, plus `mergeGeometryStreamsInLaneOrder` (added after R34's initial landing to close its own "documented follow-up"): SIMD-lane stream-range reservation via a checked prefix sum, merging one per-lane builder into a combined one in deterministic lane order, rejecting a lane's (and every later lane's, for that stream) whole reservation rather than overflowing the combined builder's declared capacity, and forcing a strip boundary at every lane edge even when a lane's own trailing strip was left open; layered-rendering array-layer selection that discards rather than clamps an out-of-range index (`feme::graphics::resolveRenderTargetArrayLayer`, new LayeredRendering.h, plus `AttachmentView::ArrayLayers`); and, added after R34's initial landing to begin closing its largest deferred item, `feme::cpu::HullWrapperPass` (new HullWrapper.h/.cpp) plus `FemePatchArgs`/`PreparedPatchBatch`/`CompiledStage::invokePatch`: the control-point phase of a real hull entry point, compiled through the CPU lowering pipeline into an invokable batch, for the common per-control-point-independent shape (each control point reads only its own input control point's attributes, addressed by `StageLayoutSystemValue::OutputControlPointID`) -- see HullWrapper.cpp's file comment for why this phase alone needs none of `EntryWrapperPass`'s barrier-region-splitting machinery (the patch-constant function is a separate compiled entry receiving the *completed* `OutputPatch`, so the phase boundary itself is the synchronization point) and what two shapes remain diagnosed rather than silently mishandled (a control point indexing a sibling control point's input, and a group-sync barrier within the phase); and, added in a further follow-up session to close that same deferred list's first item, `feme::cpu::PatchConstantWrapperPass` (new PatchConstantWrapper.h/.cpp) plus `FemePatchConstantArgs`/`PatchConstantResources`/`PreparedPatchConstantBatch`/`CompiledStage::invokePatchConstant`: the hull shader's second phase, a single non-batched invocation per patch that may read any (not only its own) output control point of the completed `OutputPatch` and writes tessellation factors/patch constants to unbatched per-patch storage, still routed through the general SIMDize/WaveLowering machinery but invoked with only lane 0 active rather than a wave loop over some batch count. `feme::cpu::isPatchConstantPhase` (new HullPhase.h/.cpp, private to `lib/Transforms/CPU`) discriminates a hull-stage function's two phases -- Direct3D/Vulkan give the patch-constant function no stage of its own -- by checking for a `SignatureDirection::PatchOutput` element, which only the patch-constant phase ever writes; `HullWrapperPass` now skips a candidate this identifies as the patch-constant phase. And, added in a further follow-up session to close that deferred list's remaining, "smaller, more scoped" item, an `InputPatch` parameter on the patch-constant function (the original, pre-control-stage input control points, distinct from the completed `OutputPatch`): `FemePatchConstantArgs` grows a second, independent structure-of-arrays input block (`InputPatch`/`InputPatchLayout`/`InputPatchControlPointCount`), and `SignatureElement::FromInputPatch` on a `SignatureDirection::Input` element tells `PatchConstantWrapperPass`'s `lowerPatchConstantInputLoad` which of the two blocks a given `feme.stage.input.load` addresses. And, added in a further follow-up session to close that deferred list's own largest remaining item, `feme::cpu::DomainWrapperPass` (new DomainWrapper.h/.cpp) plus `FemeDomainInvocation`/`FemeDomainArgs`/`DomainResources`/`PreparedDomainBatch`/`CompiledStage::invokeDomain`: the domain (evaluation) stage, batched one independent invocation per tessellator-generated domain point exactly the way `VertexWrapperPass` batches vertices, with each `feme.stage.input.load` routed by the signature element it names to one of this stage's three input sources -- the completed patch's control points (`SignatureDirection::Input`, readable at any control-point index, since evaluating a patch means blending its control points), the per-patch tessellation factors/patch constants (`SignatureDirection::PatchInput`, addressed by row/component alone, the mirror image of the patch-constant phase's own unbatched output store), and `SV_DomainLocation` (`SignatureSystemValue::DomainLocation`/`StageLayoutSystemValue::DomainLocation`, read from the per-invocation `FemeDomainInvocation` record the way a vertex batch reads `SV_VertexID` from its own) -- writing ordinary per-vertex outputs, since a domain shader's result is a vertex; a dynamically indexed domain-location component (the record is a fixed-size ABI struct) and a group-sync barrier (domain invocations are independent) are diagnosed rather than silently mishandled. And, added in a further follow-up session to close that deferred list's last remaining "wrapper" item, `feme::cpu::GeometryWrapperPass` (new GeometryWrapper.h/.cpp) plus `FemeGeometryInvocation`/`FemeGeometryArgs`/`GeometryResources`/`PreparedGeometryBatch`/`CompiledStage::invokeGeometry`: one invocation per assembled input primitive, batched over `FemeGeometryArgs::PrimitiveCount` exactly the way `VertexWrapperPass` batches vertices, reading a structure-of-arrays input block addressed `primitive * VerticesPerPrimitive + vertexInPrimitive` (any vertex in the primitive, not just the invocation's own, unlike the hull control-point phase's own restriction -- an adjacency triangle's "opposite" vertices, for instance). `feme.stage.stream.emit`/`.cut` (`StageOpKind::StreamEmit`/`StreamCut`) turn ordinary per-invocation output-store scratch storage into the stage's real, bounded, variable-count result: `emit` snapshots that scratch storage into one record of three flat, host-owned arrays rather than calling back into a live `GeometryStreamBuilder` object from JIT-compiled code (no precedent in this codebase for that), and `feme::graphics::collectGeometryStreams` (new GeometryStreamCollection.h/.cpp, living in `feme::graphics` since `FeMeTargetCPU` does not depend on `FeMeGraphics`) replays those flat records back into one real `GeometryStreamBuilder` per primitive and merges them via `mergeGeometryStreamsInLaneOrder`, finally closing that function's own "driving it from a real widened invocation" deferral. This also closed a latent gap: `feme.stage.stream.emit`/`.cut` needed the same per-lane side-effect-mask threading `feme.stage.output.store` already had (`LinearizePass` now creates masked variants of them, and `FunctionWidener` widens those variants in SIMDize.cpp), since without it SIMDize left a uniform-operand `feme.stage.stream.emit`/`.cut` call completely unwidened, firing once per *wave* rather than once per active *lane*. Two shapes remain diagnosed rather than silently mishandled: more than one output stream (this milestone's `FemeGeometryArgs` only carries storage for stream 0), and a group-sync barrier (geometry invocations are independent, like the domain stage's). (Crack-free non-uniform per-edge tessellation, previously deferred here, was added after R34's initial landing -- see `bridgeRingsByEdge` in Tessellator.cpp.) And, added in a further follow-up session to begin closing the "wiring the compiled hull/domain/geometry stages into `executeDraws`/`feme-render`" item (the last of the two items the prior session's open-issue list carried), three pieces of host-side marshaling glue, each unit-tested standalone ahead of a real chained draw exercising them together: `feme::graphics::PatchRecord` (Patch.h) grows a second, independent structure-of-arrays block for a patch's original input control points (`writeInputControlPoint`/`readInputControlPoint`, alongside the existing output-control-point storage), which `PatchConstantWrapperPass`'s `InputPatch` parameter needs a per-patch home for; `feme::graphics::buildDomainInvocations` (new DomainInvocations.h/.cpp) converts a `feme::graphics::tessellate` result's `DomainPoint`s into a `feme::cpu::FemeDomainInvocation` array for `FemeDomainArgs::Invocations`; and `feme::graphics::buildGeometryInputs`/`buildGeometryInvocations` (new GeometryInputs.h/.cpp) gather an assembled primitive batch's vertex-stage output attributes into `FemeGeometryArgs::Inputs`'s primitive-major layout (given the same per-primitive vertex-index lists `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency` already produce) and build one `FemeGeometryInvocation` per primitive's `SV_PrimitiveID`. Still deferred, documented in HullWrapper.cpp's and GeometryWrapper.cpp's own comments and in this row's own history above: generalizing `EntryWrapperPass`'s barrier-region splitting to the control-point batch ABI for a hull shader that needs it, and actually chaining the four compiled stage invocations (hull control-point phase, patch-constant phase, domain, geometry) together per patch/primitive and wiring the result into `executeDraws`/`feme-render` -- this session's glue narrows that gap but does not close it: `feme::graphics::Executor` does not yet call `invokePatch`/`invokePatchConstant`/`invokeDomain`/`invokeGeometry` at all. `unittests/Graphics/{Tessellator,Patch,GeometryStream,GeometryStreamCollection,LayeredRendering,DomainInvocations,GeometryInputs}Test.cpp`, `unittests/Transforms/CPU/{HullWrapper,PatchConstantWrapper,DomainWrapper,GeometryWrapper}Test.cpp` (including `LowersInputPatchAndOutputPatchReadsSeparately` and `LowersAllThreeInputSourcesAndBuildsWrapper`), `unittests/Target/CPU/CompiledStageTest.cpp`'s `InvokePatch{,Constant}RunsStageAwarePath`, `InvokePatchConstantReadsInputPatchSeparatelyFromOutputPatch`, `InvokeDomainRunsStageAwarePath` and `InvokeGeometryRunsStageAwarePath` cases, and `PipelineTest.cpp`'s/`SignatureTest.cpp`'s/`StageOpsTest.cpp`'s new cases cover today's scope; `ninja check-feme` (assertions-enabled, ccache build) passes in full before and after -- G5 is not yet complete, since no image-comparison completion test exists) | G5 | §1.8.5 | R33, R24 |
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
| V0 | Loader-visible skeleton: optional Vulkan-Headers dependency, `vk.xml` entrypoint generator, hidden-visibility ICD with a version script and development manifest, instance/physical device/device/compute queue, truthful properties and limits, loader smoke and two-ICD coexistence tests (done: `feme/utils/vk_gen_entrypoints.py` generates the entrypoint table directly from vk.xml; `lib/Vulkan/Objects.h`/`PhysicalDeviceInfo.cpp`/`EntryPoints.cpp` implement the object model, truthful Vulkan 1.0/1.1 core properties/limits/memory/queue-family data and the host-derived pinned subgroup size; `VulkanICD.cpp`/`libfeme_vulkan.map` export exactly the four loader-facing symbols; `feme-vulkan-loader-smoke` plus `test/Vulkan/{loader-smoke,two-icd-coexistence}.test` cover the real Khronos loader and Mesa lavapipe coexistence; see FeMeVulkanDesign.md's "V0" Status note for its two scope deviations -- advertising `apiVersion` 1.1 rather than 1.2, and deriving the host vector width without standing up a full `TargetMachine`) | — (new build-system work, §1.9) |
| V0.5 | SPIR-V import that survives real shaders: a glslang/DXC/Clang corpus, the decision between fixing MLIR's structurized deserializer and translating the SPIR-V CFG to unstructured LLVM IR for `PreparePass` to restructure, a prototype of the chosen approach, and the importer fuzzer extended to it (done: decided in favor of the unstructured path, implemented as `SPIRVImporter`'s unconditional default rather than a failure-triggered fallback -- `ImportOptions::SPIRVEnableControlFlowStructurization` now defaults to `false`, see FeMeVulkanDesign.md's "SPIR-V import prerequisites" Status note for the downstream `spirv.mlir.loop` -> `llvm` dialect conversion crash the prototype found, which is *why* it had to be the unconditional default rather than a retry. Validated against a DXC-compiled corpus, `feme/test/Tools/feme-run/SPIRV/{diamond,loop-merge-phi}.hlsl` (gated on a new `system-dxc` lit feature), plus the pre-existing `llc`/SPIRV-backend fixtures; `feme-spirv-import-fuzzer`'s seed corpus gained a matching unstructured, multi-block seed. glslang was unavailable in this pass's environment, so the corpus has no GLSL-sourced entry, and the `OpCopyObject` failure mode was not reproduced (left open, not confirmed fixed) -- both remain for whoever next extends the corpus) | — (may change V1's design; schedule before V1) |
| V1 | Empty compute dispatch: memory, buffers, shader modules, pipeline layouts, command pools/buffers, group-size resolution, submit/fences/idle, direct, base and indirect dispatch (done: `feme/lib/Vulkan/{Memory,Buffer,Pipeline,CommandBuffer,Sync,GroupSize}.{h,cpp}` implement the full bullet list; `SyncTest.SubmitDispatchAndWaitOnFence` (feme/unittests/Vulkan/SyncTest.cpp) is the milestone's own end-to-end scenario, submitting a real compiled empty compute dispatch to a queue and observing its fence signal. See FeMeVulkanDesign.md's "V1" Status note for its deviations: group-size resolution is a Vulkan-local raw-SPIR-V scan rather than a `ConvertSPIRVToLLVMPass` change (MLIR's deserializer drops a `BuiltIn` decoration on a spec-constant composite entirely), `vkQueueSubmit` runs synchronously rather than through a dedicated queue thread, and dispatch execution calls `CompiledStage::invokeGroup` directly rather than through `JITEngine::dispatch`, for direct control over `vkCmdDispatchBase`'s `GroupID` offset and `vkCmdDispatchIndirect`'s runtime-read group count) | V0, V0.5, R21, R22 |
| V2 | Storage buffers and descriptors, descriptor pools/sets/updates and dynamic offsets, buffer copies and barriers, lavapipe differential (done: `feme/lib/Vulkan/{Descriptor,Pipeline,CommandBuffer}.{h,cpp}` implement descriptor set layouts/pools/sets/updates (writes and copies), `VkPipelineLayout`'s ordered descriptor-set-layout list, `vkCmdBindDescriptorSets` with dynamic offsets, buffer copy/fill/update, and `vkCmdPipelineBarrier`; dispatch execution's `buildBoundResources` materializes a `FemeDescriptor` per bound (set, binding) -- exactly `feme::cpu::BoundResourceRange`'s `(Space, BaseRegister)`, so no translation table is needed -- folding a dynamic offset into the descriptor's `Data` pointer with no shader-side change. `StorageBufferDispatchTest` (feme/unittests/Vulkan/CommandBufferTest.cpp) is the milestone's own end-to-end scenario: bind a descriptor set over two storage buffers, dispatch a shader that reads one and writes the other, and observe the result. `feme-vulkan-storage-buffer-diff` plus `test/Vulkan/storage-buffer-lavapipe-diff.test` close the lavapipe-differential bullet, running the same compiled SPIR-V against FeMe's ICD and Mesa lavapipe's and diffing their output. `maxComputeSharedMemorySize` was also raised from the core-required minimum to 32768 now that R23's prerequisite is satisfied. See FeMeVulkanDesign.md's "V2" Status note for its deviations: per-descriptor-type pool-size accounting is not modeled (only `maxSets` is enforced), and update-after-bind/descriptor update templates remain unimplemented) | V1, R26, R23 |
| V3 | Push constants onto FeMe root constants, uniform buffers, binary and timeline semaphores, secondary command buffers, events, query pools (done: `feme::cpu::SPIRVPushConstantLoweringPass` (plus `feme::cpu::SPIRVResourceLoweringPass`'s combined-case handling) lowers a SPIR-V push-constant global access into the CPU ABI's root-constant block, the prerequisite R25 did not cover since Vulkan push constants have no DXIL register binding at all; doing so also fixed a previously-latent MLIR SPIRVToLLVM struct-conversion bug affecting any `Block`-decorated struct with explicit member offsets (every real push-constant/uniform block), documented in SPIRVToLLVMPatterns.cpp's `convertOffsetStructTypeIgnoringDecorations`. `feme::vulkan::PipelineLayout` records `VkPushConstantRange`s and `vkCreateComputePipelines` validates full byte-for-byte coverage against `maxPushConstantsSize`; `vkCmdPushConstants` writes into new command-buffer push-constant state snapshotted into `RootConstants` per dispatch (`PushConstantDispatchTest`, feme/unittests/Vulkan/CommandBufferTest.cpp, is the milestone's own end-to-end push-constant scenario). `feme::vulkan::Semaphore` (binary and timeline, `vkCreateSemaphore`/`vkWaitSemaphores`/`vkSignalSemaphore`/`vkGetSemaphoreCounterValue`) required moving the advertised core API version and `vk_gen_entrypoints.py`'s `CORE_FEATURES` to 1.2 for the host timeline functions' core (non-`KHR`) names. `feme::vulkan::Event`/`QueryPool` (the latter accepting only `VK_QUERY_TYPE_TIMESTAMP`, since occlusion/pipeline-statistics measure rasterization/shading work this compute-only device does not perform yet) and secondary command buffers (`executeCommandBuffer`'s per-command loop factored into `executeCommandsInto`, recursed into for `vkCmdExecuteCommands`) round out the command set. `entry-wrapper-barrier-multi-wave.ll`/`multi-wave-barrier-groupshared.ll` are the first test coverage of a group spanning more than one wave, both for the barrier-splitting structure and end to end. Uniform buffers are done end to end: the Vulkan object model (`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`/`_DYNAMIC` sharing storage buffers' pool/set/dynamic-offset accounting, producing a read-only `FemeDescriptor`) and the SPIR-V `Uniform` storage-class shader-side lowering both landed -- `feme::spirv::convertUniformBlockType`/`UniformBufferAccessChainPattern` (SPIRVToLLVMPatterns.cpp) convert a uniform block to the same `spirv.VulkanBuffer` handle representation a storage buffer uses, over the block's own field struct directly rather than a runtime array, and `feme::cpu::SPIRVResourceLoweringPass` was generalized (`BufferKind::Storage`/`Uniform`) to resolve a field access to a compile-time struct-layout byte offset instead of `index * stride`; `UniformBufferDispatchTest` (feme/unittests/Vulkan/CommandBufferTest.cpp) is the milestone's own end-to-end uniform-buffer scenario. See FeMeVulkanDesign.md's "Descriptor Model" table and "V3" Status note) | V2, R25 |
| V4 | Typed buffers, `VkFormat` mapping, texel buffers, broader subgroup/atomic/robustness coverage, persistent pipeline cache with a blob fuzzer, first CTS runs over the advertised subset (done, scoped: `feme::vulkan::Format.{h,cpp}` maps every `ResourceFormat`; `VkBufferView` plus `VK_DESCRIPTOR_TYPE_{UNIFORM,STORAGE}_TEXEL_BUFFER` resolve to a `Kind::Typed` `FemeDescriptor`, with `feme::cpu::SPIRVResourceLoweringPass` normalizing the `Dim::Buffer` image handle into `createTypedLoad`/`createTypedStore` -- scoped to the formats (`R32G32B32A32_{SFLOAT,UINT,SINT}`, `R8G8B8A8_{UNORM,SNORM,UINT,SINT}`) the CPU runtime's typed-load/store helpers implement a conversion for (`femeCpuResourceLoadTypedV4F32`/`StoreTypedV4F32` and, added in a later V4 pass alongside `isSupportedTexelElementType`'s `<4 x i32>` acceptance, `femeCpuResourceLoadTypedV4I32`/`StoreTypedV4I32`; the `R8G8B8A8_{SNORM,UINT,SINT}` packed-byte conversions were added in a still-later V4 follow-up pass), enforced at `vkCreateBufferView` by `feme::vulkan::isTexelBufferFormatSupported` (previously unenforced: any `mapVkFormat`-recognized format was silently accepted rather than rejected); `robustBufferAccess` is now advertised (bounds checking was already unconditional); `feme::cpu::SIMDizePass` lowers `SubgroupSize`/`SubgroupLocalInvocationId`, previously raised with no CPU-target lowering at all; `feme::vulkan::PipelineCache` implements `vkCreate/Destroy/GetData/MergePipelineCaches` (previously entirely unimplemented) with a header/UUID/digest-validated blob and `feme-vulkan-pipeline-cache-fuzzer`; `feme/utils/filter_vulkan_cts_cases.py` plus a `system-vulkan-cts`-gated lit test give the CTS bullet its infrastructure. See FeMeVulkanDesign.md's "V4" Status note for what remains: no `spirv.Atomic*` op has any dialect-conversion pattern at all -- a gap this milestone's investigation surfaced, not a previously-tracked one -- so atomic buffer access is unraised entirely; a texel buffer's narrower-than-`<4 x T>` channel-count formats and the remaining 16-bit packed formats (`R16G16B16A16_*`, `R11G11B10_FLOAT`, `R10G10B10A2_*`) still have no runtime conversion; the pipeline-cache blob carries no relocatable object code (an in-process hit is a real skip, a cross-process one is not); and no actual CTS run happened -- `deqp-vk` was not available in this environment) | V3, R22 |
| V5 | Images and sampling: image memory requirements, views, layout tracking, copies, storage and sampled images, samplers (done, scoped: `feme::vulkan::Image`/`ImageView`/`Sampler` (lib/Vulkan/Image.{h,cpp}) implement `vkCreateImage`/`vkGetImageMemoryRequirements{,2}`/`vkBindImageMemory{,2}`/`vkCreateImageView`/`vkCreateSampler`, a packed mip-major subresource layout computed once at creation time into `feme::cpu::FemeImageSubresourceLayout` (R29's own ABI, landed ahead of this milestone), and per-subresource `VkImageLayout` tracking; `vkCmdCopyBufferToImage`/`vkCmdCopyImageToBuffer`/`vkCmdCopyImage` and a `vkCmdPipelineBarrier` image memory barrier's layout transition (lib/Vulkan/CommandBuffer.{h,cpp}) round out the copy/layout bullets; `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE`/`_STORAGE_IMAGE`/`_SAMPLER`/`_COMBINED_IMAGE_SAMPLER` (lib/Vulkan/Descriptor.{h,cpp}) let a descriptor set hold an image/sampler binding alongside its existing buffer ones. `unittests/Vulkan/ImageTest.cpp` and new `DescriptorTest`/`ProcAddrTest` coverage exercise all of it, plus a follow-up pass (`feme/test/Vulkan/image-loader-smoke.test`, `tools/feme-vulkan-image-loader-smoke`) that exercises the same create/copy path through the real Khronos loader rather than only `libfeme_vulkan` directly, and widened this milestone's own narrower deviations: multisample images are now accepted at the object-model level (up to 4 samples for a sampled/storage 2D image; `vkCmdCopyImage` copies every sample verbatim, but no shader/render-target path reads one yet), `vkCmdCopyImage` now requires only a matching texel size rather than an identical `VkFormat` (matching real Vulkan's own "compatible formats" rule -- no value conversion, on either side, either before or after this change), and `vkCreateSampler` explicitly rejects a chained `VkSamplerCustomBorderColorCreateInfoEXT`/`VkSamplerBorderColorComponentMappingCreateInfoEXT`, not only the `VkBorderColor` `..._CUSTOM_EXT` enumerators it already rejected. This milestone's one remaining deviation -- "a real dispatch still cannot *consume* an image or sampler" -- is now closed by R30's follow-up: `buildBoundResources` materializes `FemeImageDescriptor`/`FemeSamplerDescriptor` arrays from a set's image/sampler bindings, `compileComputePipeline` validates each bound range against a descriptor type of its own class instead of rejecting `UsesSamplerHeap` outright, and `SampledImageDispatchTest` plus `feme-vulkan-sampled-image-smoke`/`sampled-image-loader-smoke.test` exercise a real dispatch that samples a bound image, the latter through the real Khronos loader. See FeMeVulkanDesign.md's "V5" Status note for the narrower shader-side scope that remains, all of it inherited from R30) | V4, R30 |
| V6 | Graphics queue and basic rendering: graphics stage compilation, `VkRenderPass` and dynamic rendering, graphics pipeline state, draws, and `VK_QUEUE_GRAPHICS_BIT` (done, scoped: `feme::vulkan::GraphicsPipeline` (lib/Vulkan/GraphicsPipeline.{h,cpp}) compiles a SPIR-V vertex/fragment pair through the compute path's own import/translate flow plus `feme::graphics::CanonicalizeStagePass`, validates the cross-stage interface against the core reflection, and translates every `VkGraphicsPipelineCreateInfo` state block into `feme::graphics`' normalized pipeline description with dynamic state resolved per draw; `feme::vulkan::RenderTargetBinding` (lib/Vulkan/RenderPass.{h,cpp}) is the one internal shape `VkRenderPass`/`VkFramebuffer` and `vkCmdBeginRenderingKHR` both normalize into; the command set grows by render pass instances, vertex/index binding, the implemented `vkCmdSet*` subset, direct and indirect draws with once-read, bounds-checked arguments, and `vkCmdClear{ColorImage,DepthStencilImage,Attachments}`/`vkCmdBlitImage`/`vkCmdResolveImage` (lib/Vulkan/ImageOps.{h,cpp}); `VK_QUEUE_GRAPHICS_BIT` joins the existing universal queue family and the framebuffer sample-count limits become real contracts. Two compiler-side prerequisites landed with it: a SPIR-V graphics builtin now keeps its `BuiltIn` decoration through the stage-IO conversion and maps onto `feme::SignatureSystemValue`, and a vector-typed interface access is decomposed per component for `SIMDizePass`. `unittests/Vulkan/{GraphicsPipeline,Draw,RenderPass,ImageOps}Test.cpp` (the latter now including depth, stencil, blend, MRT, and multisample-resolve draws) plus `feme-vulkan-graphics-smoke`/`test/Vulkan/graphics-loader-smoke.test` (the same scene through the real Khronos loader) cover it, and a follow-up pass closed the lavapipe half of this milestone's own CTS/differential bullet: `test/Vulkan/graphics-lavapipe-diff.test` runs seven scenarios -- a `VkRenderPass`, dynamic rendering, depth, stencil, blending, MRT, and a multisample resolve -- against both FeMe and Mesa lavapipe and diffs the results byte-for-byte. See FeMeVulkanDesign.md's "V6" Status note for the deviations and what a follow-up pass closed -- every unsupported state combination fails at creation rather than at draw time, dynamic rendering is exposed as `VK_KHR_dynamic_rendering` since the advertised version is 1.2, no `deqp-vk` CTS run happened since it was unavailable in this environment, and the lavapipe differential covers seven scenarios rather than "every format and state combination the driver reports"; per-instance vertex input rate, primitive restart on indexed triangle strips, and a graphics pipeline-cache entry (`translateFixedFunctionState` now runs every fixed-function translation before either stage compiles, so its result plus both stages' SPIR-V and the pipeline layout can be hashed into a key checked *before* paying for compilation) were all closed in follow-up passes, and `vkCmdBlitImage` now converts between differing formats and mirrors a region along either axis) | V5, R32, R33 |
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

The Vulkan conformance steps C1–C10 (§1.9.1) are not a milestone row in
this table. They cut across V4–V7 rather than following them, they are
ordered by measured CTS cost rather than by feature dependency, and C1/C4a
in particular should be done *before* the next V milestone, since they are
what makes the next CTS run's output readable.

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
