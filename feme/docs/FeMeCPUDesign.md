# FeMe CPU Target Design

## Status

Proposal / first draft. Nothing described here is implemented yet. This
document is a companion to [Design.md](Design.md) — it does not restate
FeMe's architecture, only the parts that are new for CPU targets. Read the
"Pipeline Abstraction", "Retargeting to Native ISA", and "Raised LLVM IR ->
AMDGPU" sections of that document first; this design is a sibling of the
latter.

Open questions that need answers before this leaves draft state are
collected in "Open Questions" at the end.

## Summary

FeMe can already import DXIL and SPIR-V, raise both into a common,
format-agnostic "raised" LLVM IR, and retarget that IR to AMDGPU or back to
DXIL/SPIR-V. This document proposes a fourth destination: **the host CPU**,
executed either as an object file or through an in-process **JIT**.

A GPU shader is an SPMD program: the source describes the behaviour of a
single invocation ("lane"), and the machine supplies the parallelism. A CPU
has no such machine. Something has to *choose* how many invocations execute
per hardware thread and rewrite the program accordingly. That choice is this
design's central knob: a **user-provided wave size** `W`. The program is
transformed from "one lane per program" into "`W` lanes per program", with
every lane-varying value widened to a `<W x T>` vector, every divergent
branch replaced by an execution mask, and the shader's own wave intrinsics
(`WaveActiveSum`, `WaveReadLaneAt`, ...) lowered to ordinary vector
operations over that mask.

Three pieces are needed beyond the transform itself:

1. A **resource binding model** — a shader refers to its resources
   indirectly, by (register space, register); a CPU program has to get real
   pointers from somewhere. This design proposes a flat, explicit
   **descriptor table ABI** passed to the kernel, rather than the
   one-pointer-argument-per-binding scheme
   `feme::amdgpu::ResourceLoweringPass` uses, because a CPU host is expected
   to bind arrays, change bindings between dispatches, and reuse one
   compiled kernel across binding sets.
2. A small **runtime support library** for the operations that do not lower
   to plain IR (typed-buffer format conversion, atomics on formats, and the
   host-side dispatch loop).
3. A **JIT flow** built on ORC, following FeMe's no-global-state rule, so a
   host process can compile and dispatch a shader without touching the file
   system.

## Motivation

- **Reference/fallback execution.** Running a shader without a GPU (or
  without a *conformant* GPU) is how correctness questions get answered:
  WARP, `lavapipe`, and SwiftShader all exist for this reason. FeMe already
  has the front half of such a tool; the CPU target is the back half.
- **Testing FeMe itself.** Every test FeMe has today checks *IR shape*: that
  a `dx.op.*` call became the right intrinsic, that a handle got the right
  target type. None of them check that the translated program *computes the
  right answer*, because there is no way to run it in `lit`. A CPU target
  plus a tiny dispatch tool turns "did we translate this correctly?" into an
  executable question, on any CI machine, with no GPU and no driver.
- **Shader debugging and analysis.** Once a shader is ordinary host code,
  ordinary host tools apply: debuggers, sanitizers, profilers.
- **Compute offload.** A host that already has a DXIL/SPIR-V compute kernel
  and no GPU to run it on can run it on the CPU rather than maintaining a
  second, hand-written implementation.

The first two are the ones driving this design; the second in particular is
what makes the JIT flow a v1 deliverable rather than a follow-up.

## Goals

- Retarget an already-raised `llvm::Module` (from either DXIL or SPIR-V
  import) to the host CPU, as a `feme::Backend` selected the same way every
  other target is (`--target=<host triple>`), reusing
  `feme::TargetMachineBackend` for the final codegen step.
- Support a **user-provided wave size** `W` ∈ {1, 2, 4, 8, 16, 32, 64, 128},
  independent of the host's native vector width, with `W = 1` a supported
  (scalar, one-lane-per-program) configuration that shares the entire
  pipeline.
- Preserve the semantics of the wave/quad intrinsics FeMe already raises,
  relative to that wave size.
- Define a resource binding ABI that a host can populate without knowing how
  the shader was compiled, and that survives the shader being recompiled at
  a different wave size.
- Provide an in-process JIT (`feme::cpu::JITEngine`) and a dispatch entry
  point, both `feme::Context`-scoped and free of process-wide mutable state.
- Be testable phase by phase: each transform is an individually
  `feme-opt`-runnable pass with its own `lit` tests, and the whole thing is
  additionally testable by *running* shaders and checking their output
  buffers.

## Non-Goals (for now)

- **Performance parity with a hand-written CPU rasterizer/kernel.** The
  target is correct, reasonably vectorized code, not beating ISPC. Notably,
  this design does no lane-reordering, no repacking of divergent work, and
  no dynamic wave compaction.
- **Graphics pipeline stages.** Compute (`hlsl.shader = "compute"` /
  SPIR-V `GLCompute`) only, at least initially. Vertex/pixel shaders need a
  pipeline around them (rasterization, interpolation, blending) that is a
  much larger project than the shader transform itself, and none of FeMe's
  driving use cases need it yet. The transform is stage-agnostic; the
  *wrapper* and resource model are not.
- **Texture sampling.** Filtering, addressing modes, mip selection and
  format decode are a large body of work with no representation in FeMe's
  raised IR yet (`ResourceLoweringPass` explicitly doesn't handle texture
  handles either). Typed/structured/raw buffers and constant buffers only.
- **Derivatives / quad ops** (`ddx`, `ddy`, `QuadReadAcross*`): these need a
  defined 2x2 lane arrangement, which only makes sense once pixel shaders
  do. `WaveGetLaneIndex`-style quad ops on a compute shader could be
  supported later by fixing a lane-to-quad mapping.
- **Indirect calls and recursion.** Neither appears in DXIL or in the
  SPIR-V subset FeMe imports today.
- **Debug info fidelity.** Preserving line tables through the SIMD-izer is
  desirable and cheap for straight-line code; guaranteeing anything about
  variable locations after widening is not attempted.

## Prior Art

This is a well-trodden problem; the design deliberately follows the
established solutions rather than inventing.

| System | Approach | What this design takes from it |
|---|---|---|
| Whole-Function Vectorization (Karrenberg & Hack, 2011) and the Region Vectorizer | Divergence analysis → CFG linearization with masks → value widening | The overall three-phase shape, and the "analyze, linearize, widen" separation into distinct passes |
| ISPC | SPMD-on-SIMD with a fixed program count ("gang size"), mask stack | Wave size as an explicit compile-time constant; masked memory ops; "all lanes off" branch skipping |
| POCL, Intel's OpenCL CPU runtime | Work-item loops, barrier-delimited regions | The barrier model: split the kernel at barriers into regions, wrap each region in a loop over the waves of a group |
| llvmpipe / SwiftShader | JIT shaders to host code behind a driver | The JIT/ORC flow and the "compile once, dispatch many" split |
| LLVM in-tree | `UniformityInfo`, `StructurizeCFG`, `FixIrreducible`, `UnifyLoopExits`, masked load/store/gather/scatter intrinsics, ORC | Essentially all of the machinery — see below |

The single most important consequence: **almost none of the hard analysis is
new code**. LLVM's `GenericUniformityInfo` already implements
divergence/sync-dependence over an arbitrary "is this value lane-varying?"
predicate, and `StructurizeCFG` already turns reducible divergent control
flow into the structured form a mask-based linearizer wants.

## Execution Model

A dispatch is a 3D grid of **thread groups**; each group is a 3D block of
**invocations** whose dimensions come from `hlsl.numthreads` (recovered by
`feme::dxil::MetadataRaisingPass` for DXIL, and by FeMe's SPIR-V → `llvm`
dialect conversion for SPIR-V — see Design.md). This design adds two levels
between "group" and "invocation":

```
dispatch  = grid of groups                     (host loop, parallel)
group     = ceil(GroupSize / W) waves          (host loop or wave loop, sequential per group)
wave      = W lanes, one SIMD program          (the transformed function)
lane      = one original shader invocation     (one element of every <W x T>)
```

**Lane linearization.** Lane `i` of wave `w` is the invocation with
flattened in-group index `w * W + i`, where the flattened index is
`x + y * NumThreadsX + z * NumThreadsX * NumThreadsY`. This is exactly
HLSL's `SV_GroupIndex` / SPIR-V's `LocalInvocationIndex` ordering, which
means `llvm.dx.flattened.thread.id.in.group` lowers to
`splat(w * W) + iota` — the cheapest possible thing — and every other
builtin is derived from it.

**Partial waves.** `GroupSize` need not be a multiple of `W`. The final wave
of a group runs with an entry mask that has the out-of-range lanes off,
rather than the kernel being specialized per group. When
`GroupSize % W == 0` (the common case, and always true for `W = 1`) the
entry mask is all-ones and every mask expression folds away.

**Wave size semantics.** `W` is what the shader observes:
`WaveGetLaneCount()` returns `W`, `WaveGetLaneIndex()` returns the lane's
index in `[0, W)`, and every `WaveActive*` reduction reduces over exactly
those `W` lanes, honouring the current execution mask (inactive lanes do not
contribute, matching both DXIL's and SPIR-V's definitions). A shader that
declares a required wave size (`[WaveSize(n)]` / SM 6.6+
`!dx.entryPoints`'s wave-size tag, SPIR-V's
`SubgroupSize` execution mode) and is compiled at a different `W` is a
diagnosable error, not silently miscompiled.

**Independence from host vector width.** `W` is a *semantic* choice, not a
codegen one: `<32 x float>` on a host with 128-bit vectors is legal, and
LLVM's type legalizer splits it into 8 operations. Choosing `W` to match the
host (`W = VectorBits / 32`) is the performance-sensible default, but
correctness never depends on it, and the ability to compile at the wave size
a shader was *written* for (e.g. 32, for a shader whose algorithm assumes
`WaveGetLaneCount() == 32`) matters more than the codegen quality.

## Pipeline Overview

```mermaid
flowchart TD
    DXIL[DXIL] -- Importer + OpRaising + MetadataRaising --> R[raised llvm::Module<br/>llvm.dx.* / llvm.spv.*]
    SPV[SPIR-V] -- Importer + SPIRVToLLVM --> R
    R --> P1[feme-cpu-prepare<br/>canonicalize + structurize CFG]
    P1 --> P2[feme-cpu-lower-resources<br/>descriptor table ABI]
    P2 --> P3[feme-cpu-linearize<br/>divergence -> masks]
    P3 --> P4[feme-cpu-simdize<br/>widen to &lt;W x T&gt;]
    P4 --> P5[feme-cpu-lower-wave<br/>wave/builtin intrinsics]
    P5 --> P6[feme-cpu-wrap-entry<br/>group/wave loops, barriers]
    P6 --> TM[TargetMachineBackend<br/>host triple]
    P6 --> JIT[feme::cpu::JITEngine<br/>ORC]
```

Each box is a separate pass with its own `feme-opt` name and its own `lit`
tests, following the precedent set by `feme-dxil-raise-ops` /
`feme-amdgpu-lower-{raised,resources}`. The split points are chosen so that
each pass's input and output are both *printable, checkable* LLVM IR:

- After `feme-cpu-lower-resources`, resources are pointers and the module has
  no `handlefrombinding` left — checkable without reasoning about masks.
- After `feme-cpu-linearize`, control flow is (almost) straight-line and
  masks are explicit `i1` values — checkable without reasoning about
  vectors.
- After `feme-cpu-simdize`, everything is `<W x T>` — checkable without
  reasoning about the group wrapper.

The resource lowering runs *before* linearization/widening deliberately: it
is the one phase whose correctness has nothing to do with `W`, so it should
be testable at `W`-agnostic scale, and lowering handles to plain pointers
early lets the widener treat them as ordinary uniform values.

## Phase 1: Preparation (`feme::cpu::PreparePass`)

Gets the raised module into the shape the later phases assume:

- **`feme::dxil::IntrinsicExpansionPass`** (already exists) for the DXIL-only
  intrinsics with no direct CPU equivalent.
- **Reject or handle unstructured control flow**: `FixIrreducible` then
  `StructurizeCFG` (both in-tree, both target-independent). SPIR-V input
  already went through MLIR's structurizer during import; DXIL input has not
  and can be arbitrarily unstructured. `UnifyLoopExits` runs alongside, as
  `StructurizeCFG` requires it.
- **`LowerSwitch`**: the linearizer handles two-way branches only.
- **Promote what can be promoted** (`mem2reg`/SROA): an `alloca` that stays
  in memory becomes a per-lane array in Phase 4 (see below), which is
  correct but much worse code, so it is worth running SROA first.
- **Canonicalize entry points**: exactly one `hlsl.shader="compute"`
  function is selected (by name, from options), and any other entry point is
  either dropped or left alone; the wrapper in Phase 6 needs a single root.

Nothing here is FeMe-specific except the pass ordering and the entry point
selection, so this pass is mostly a pipeline builder.

## Phase 2: Uniformity Analysis (`feme::cpu::WaveUniformityInfo`)

Widening every value to `<W x T>` would be correct and slow. The interesting
question is which values are *lane-varying* (divergent) and which are
uniform across the wave; uniform values stay scalar, uniform branches stay
branches.

LLVM's `llvm::UniformityInfo` (`GenericUniformityInfo<SSAContext>`) already
implements exactly this analysis, including the hard part (sync dependence:
which values become divergent because of *where* control flow reconverged).
It is driven entirely through `TargetTransformInfo`:
`hasBranchDivergence()`, `getValueUniformity()`, `isUniform()`. Neither the
`DirectX` nor the `SPIRV` target implements those hooks, and the host target
(x86, AArch64) answers "no divergence" — so FeMe supplies its own:

```c++
/// A TargetTransformInfo implementation describing the SPMD execution model
/// of a raised shader, independent of the host it will run on: branches are
/// divergent, and the lane-varying builtins are the sources of divergence.
class WaveTTIImpl : public llvm::TargetTransformInfoImplBase { ... };

/// Computes UniformityInfo for `F` under the SPMD model.
llvm::UniformityInfo computeWaveUniformity(llvm::Function &F,
                                           llvm::DominatorTree &DT,
                                           llvm::CycleInfo &CI);
```

Divergence sources are the lane-varying builtins FeMe already raises:
`llvm.{dx,spv}.thread.id`, `.thread.id.in.group`,
`.flattened.thread.id.in.group`, `llvm.dx.wave.getlaneindex`, and every wave
op whose result is per-lane (`WavePrefix*`, `WaveReadLaneFirst` is *uniform*,
etc.). Everything else — group ids, resource handles, constants, loads of
uniform addresses — is uniform by inference.

This is an analysis, not a transform, and gets `gtest` coverage directly
(construct IR, assert the expected values are divergent) plus a
`feme-opt -passes='print<feme-cpu-uniformity>'` printer so `lit` tests can
check it the way `print<uniformity>` does upstream.

**Alternative considered:** teaching the in-tree `DirectX`/`SPIRV` TTIs
these hooks upstream, so `UniformityInfoAnalysis` works out of the box.
That's arguably where this belongs long-term, and is a strictly larger
change (it affects those targets' own pipelines); FeMe's own TTI is not
mutually exclusive with it and can be deleted later.

## Phase 3: Linearization and Predication (`feme::cpu::LinearizePass`)

Turns divergent control flow into data flow over an explicit execution mask,
before any widening happens. Working on scalar IR here (masks are `i1`, not
`<W x i1>`) keeps this pass's tests readable and its logic independent of
`W`.

- Each block gets an **entry mask** value: the disjunction of the edge masks
  reaching it, where an edge mask is the predecessor's mask conjoined with
  the (possibly negated) branch condition.
- **Divergent two-way branches** become unconditional fallthrough; both
  sides execute under their masks. Blocks are visited in a topological order
  of the structurized CFG, so a mask is always available when needed.
- **Divergent `phi`s** become `select`s of the incoming edge masks.
- **Loops with divergent exits** keep their backedge, but the latch's
  condition becomes "any lane still active", and the loop body runs under a
  per-iteration active mask (lanes that exited are masked off for the
  remaining iterations). Values live out of the loop are captured under the
  exit mask into a loop-carried value.
- **Uniform branches stay branches.** This is the entire payoff of Phase 2,
  and also the mechanism for the "skip a block when all lanes are off"
  optimization: a *divergent* branch whose taken block is expensive can be
  guarded by a uniform `if (mask != 0)` test. Whether to do that by default,
  always, or by heuristic is an open question below.
- **Early `ret`** under divergence becomes a mask update plus a jump to a
  unified exit; the shader's "still running" mask is conjoined into every
  subsequent block's mask.
- **Side-effecting operations** (stores, atomics, resource writes) are *not*
  rewritten here — they are annotated with their governing mask (via an
  operand bundle or a FeMe-internal `llvm.feme.cpu.mask` token, TBD, see
  Open Questions) and Phase 4 turns them into masked forms. Loads from
  addresses that could be lane-varying are likewise annotated: an unmasked
  gather can fault on a lane that was never supposed to execute.

## Phase 4: Widening (`feme::cpu::SIMDizePass`)

Rewrites the linearized function to operate on `W` lanes. The pass takes `W`
as an explicit option (`feme-opt -passes=feme-cpu-simdize -feme-wave-size=8`).

| Construct | Widened form |
|---|---|
| Divergent value of type `T` | `<W x T>` |
| Uniform value | unchanged (broadcast at use sites that mix) |
| Elementwise op | same op on `<W x T>` |
| `select`/mask | `<W x i1>` |
| Uniform-address `load`/`store` | unchanged, or masked when predicated |
| Divergent-address `load`/`store` | `llvm.masked.gather` / `llvm.masked.scatter` |
| Contiguous divergent address (address = base + lane*stride, stride == size) | `llvm.masked.load` / `llvm.masked.store` — worth detecting, it's the common case for `buf[tid]` |
| `alloca T` | `alloca [W x T]`, indexed by lane; SROA-able back into vectors when uniformly accessed |
| Call to a non-entry internal function | widen the callee too (whole-function vectorization of the call graph, bottom-up), passing the mask as an extra argument |
| Call to a math libcall (`llvm.sin.f32`, ...) | vector-typed intrinsic call, letting the host's vector library / scalarizer handle it |
| Atomic RMW / cmpxchg | scalarized lane loop (see below) |

**Scalarization fallback.** Any operation with no vector form is emitted as
a `W`-iteration loop (or unrolled sequence) over the lanes, guarded by the
mask. Atomics are the main user; correctness of ordering between lanes of a
wave is preserved because the lanes are genuinely sequential on a CPU.
Having this fallback is what lets the pass be *total* — it never has to bail
out on an unsupported opcode, which matters a lot for a target whose job is
"run any shader".

**Uniform-value hoisting.** Because Phase 2 already classified values, this
pass does not need to re-derive uniformity; it just never widens a value the
analysis called uniform.

## Phase 5: Wave and Builtin Lowering (`feme::cpu::WaveLoweringPass`)

Once everything is `<W x ...>`, the wave intrinsics are ordinary vector
operations. This is the phase that most justifies the whole approach — a
wave op on a GPU is a cross-lane hardware instruction, and on a CPU it's a
reduction over a vector register:

| Intrinsic | Lowering (`M` = execution mask) |
|---|---|
| `wave.getlaneindex` | `iota` (constant `<W x i32>`) |
| `WaveGetLaneCount` | constant `W` |
| `wave.is.first.lane` | lane == `cttz(bitcast M to iW)` |
| `wave.any` / `wave.all` | `reduce.or(M & X)` / `reduce.and(M ? X : true)` |
| `wave.all.equal` | broadcast of first active lane, compared under `M` |
| `wave.readlane(X, i)` | `extractelement X, i` (broadcast back) |
| `WaveReadLaneFirst` | `extractelement X, cttz(M)` |
| `WaveActiveBallot` | `bitcast (M & X) to iW`, zext to `i64`/`<4 x i32>` |
| `wave.active.countbits` | `ctpop(bitcast (M & X))` |
| `WaveActiveSum/Product/Min/Max/BitAnd/...` | `llvm.vector.reduce.*` over `select(M, X, identity)` |
| `WavePrefix*` | inclusive/exclusive scan; log2(W)-step shuffle scan, or a lane loop for large `W` |
| Thread/group ids | derived from the wrapper's loop indices (below) |

Every row here is a small, independently testable rewrite, which is how this
phase's `lit` tests are organized (one `CHECK` function per intrinsic, at a
couple of wave sizes).

## Phase 6: Group Execution and Barriers (`feme::cpu::EntryWrapperPass`)

The SIMD-ized function computes one wave. Something has to run all the waves
of a group, provide the ids they ask for, and honour barriers. This pass
produces a **wrapper function** with the fixed ABI below, containing:

```c
for (w = 0; w < WavesPerGroup; ++w)      // the "wave loop"
  wave_body(group_id, w, entry_mask(w), descriptors, groupshared);
```

**Barriers.** `GroupMemoryBarrierWithGroupSync` (DXIL `Barrier`, SPIR-V
`OpControlBarrier`) requires every invocation in the group to arrive before
any proceeds — but the wave loop runs waves one at a time to completion. The
standard fix (POCL, Intel's CPU OpenCL) is **barrier splitting**: cut the
kernel at each barrier into regions, and wrap *each region* in its own wave
loop:

```c
for (w = ...) region0(w);   // up to the barrier
for (w = ...) region1(w);   // after it
```

Values live across a barrier must be spilled to a per-wave array indexed by
`w` (a "context" allocation), since they no longer live in registers across
the split. Barriers inside divergent control flow are undefined behaviour in
both source models, so only barriers in *uniform* control flow need
handling; a barrier inside a uniform loop splits the loop itself (loop
fission over the wave loop).

**Alternative considered:** fibers/coroutines — give each wave a stack and
switch at barriers (SwiftShader does a variant of this). It handles
arbitrary barrier placement and avoids liveness spilling, but costs a
context switch per barrier per wave and drags in a coroutine/stack-switching
runtime. Barrier splitting is more code in the compiler and less at run
time, which is the right trade for FeMe. LLVM coroutines are a plausible
implementation of the fiber approach if splitting proves insufficient.

**Groupshared memory** (`addrspace(3)` in raised IR) becomes a buffer
allocated per group by the wrapper (or supplied by the caller through the
dispatch arguments if it is too large for the stack), with the address space
cast away. It is shared by all waves of the group, which is exactly right —
the wave loop is sequential, so no synchronization is needed beyond the
barrier semantics above.

**Group loops.** Whether the wrapper iterates groups too, or the host does,
is an ABI decision: this design puts *one group* per wrapper call and lets
the host parallelize across groups (see JIT flow below), because that's the
level where a thread pool wants to hand out work.

## Resource Binding Model

A shader names its resources by `(register space, register)` — plus, for
descriptor arrays, a dynamic index. `feme::amdgpu::ResourceLoweringPass`
answers this by appending one pointer argument per binding, which works
because an HSA kernel launch supplies arguments anyway. That is a poor fit
here:

- A host that wants to change one binding between dispatches would have to
  rebuild an argument buffer whose layout depends on the shader's binding
  set.
- Dynamically indexed binding arrays (`Texture2D t[] : register(t0)`) cannot
  be expressed at all — the pass explicitly gives up on them.
- The argument list changes when the shader changes, so the host cannot have
  one generic dispatch path.

Instead, the CPU target defines a **descriptor table**: a flat array of
descriptors that the kernel indexes, with the *shader-independent* layout
below.

```c
/// One bound resource. Layout is part of the CPU target ABI; see
/// feme/include/feme/Target/CPU/RuntimeABI.h.
typedef struct {
  void    *Data;        // base pointer to the resource's storage
  uint64_t SizeInBytes; // for bounds checking; 0 means unbounded
  uint32_t Stride;      // element stride (structured/typed buffers)
  uint32_t Format;      // feme::cpu::ResourceFormat, for typed buffers
  uint32_t Kind;        // typed / structured / raw / cbuffer
  uint32_t Flags;       // UAV vs SRV, ROV, counter present, ...
  void    *Counter;     // append/consume/counter UAV, else null
} FemeDescriptor;
```

Lowering (`feme::cpu::ResourceLoweringPass`, Phase 2 above):

- `llvm.{dx,spv}.resource.handlefrombinding(space, reg, range, index, ...)`
  becomes a load of `Descriptors[SlotOf(space, reg) + index]`, where the
  slot mapping is computed by the pass from the module's own bindings, in
  ascending `(space, register)` order, and **emitted into the module as
  metadata plus a queryable table** so the host knows which slot to fill.
  The dynamic `index` operand is what makes binding arrays work here and not
  in the AMDGPU scheme.
- A typed buffer access at element `i` becomes
  `Data + i * Stride` plus, when `Format` is not the shader's element type,
  a call to a runtime format conversion helper
  (`feme_rt_load_typed_f32x4` and friends). When the format is statically
  known — the common case, since the handle type spells the element type —
  the conversion is inlined and the helper never appears.
- **Bounds behaviour**: out-of-bounds reads return zero and writes are
  dropped, matching D3D/Vulkan robustness rather than trapping. The check is
  a masked compare against `SizeInBytes` and is *not* optional: a
  fault-on-OOB CPU target would turn a merely-nonconformant shader into a
  host crash, which is unacceptable for the reference-execution use case.
  An option to disable the checks (`-feme-cpu-no-robustness`) for
  performance is reasonable but should not be the default.
- Constant buffers are read-only descriptors with `Kind = CBuffer`; the
  4-component-vector-indexed access DXIL uses (`CBufferLoadLegacy`) becomes
  ordinary loads.
- Counter UAVs use `Counter` with host atomics.

`feme::cpu::ResourceLoweringPass` keeps the precedent set by its AMDGPU
sibling: a binding shape it cannot model leaves the entry point untouched
rather than half-rewritten, so it fails as a clean diagnostic.

**Descriptor table discovery.** The host needs to know the slot layout.
This design emits it as a FeMe-owned named metadata node (`!feme.cpu.bindings`)
and provides a reader (`feme::cpu::BindingTable::fromModule`) so the JIT can
hand the host a `{space, register, kind, slot}` list. That keeps the
compiled artifact self-describing, which matters for the object-file path
where there is no in-process compiler to ask.

## Kernel ABI

One exported symbol per entry point, with a `feme.cpu.entry.` name prefix
and a single argument:

```c
typedef struct {
  const FemeDescriptor *Descriptors;  // the descriptor table
  uint32_t GroupID[3];                // this dispatch item
  uint32_t GroupCount[3];             // full dispatch size
  void    *GroupShared;               // group-shared storage, or null
  void    *Reserved[4];               // ABI headroom
} FemeDispatchArgs;

void feme_cpu_entry_<name>(const FemeDispatchArgs *Args);
```

Everything the shader can ask about its position derives from `GroupID`,
`GroupCount`, and the wave loop index, so the ABI does not change with `W`,
with the shader's binding set, or between the JIT and object-file paths.
`W` and the thread group dimensions are baked into the compiled code and
reported alongside the binding table.

## JIT Flow

```c++
namespace feme::cpu {

struct JITOptions {
  unsigned WaveSize = 0;             // 0 = pick from the host's vector width
  std::string EntryPoint;            // empty = the module's only entry point
  llvm::CodeGenOptLevel OptLevel = llvm::CodeGenOptLevel::Default;
  bool EnableRobustness = true;
};

/// Owns an ORC LLJIT instance and the compiled shader in it. One per
/// compiled shader; safe to use from multiple threads to dispatch, per
/// FeMe's no-global-state rule (see Design.md).
class JITEngine {
public:
  static llvm::Expected<std::unique_ptr<JITEngine>>
  create(Context &Ctx, feme::Module M, const JITOptions &Opts);

  /// The binding table the host must fill, in slot order.
  llvm::ArrayRef<BindingInfo> getBindings() const;

  /// Runs the whole dispatch. `Descriptors` must have getBindings().size()
  /// entries.
  llvm::Error dispatch(llvm::ArrayRef<FemeDescriptor> Descriptors,
                       std::array<uint32_t, 3> GroupCount) const;
};

} // namespace feme::cpu
```

Notes and constraints:

- **ORC, not MCJIT**: `llvm::orc::LLJIT` with a `ThreadSafeModule`, which is
  the supported path and already thread-safe in the way FeMe needs.
- **Target initialization** (`InitializeNativeTarget`, `...AsmPrinter`) is
  process-global and idempotent; FeMe wraps it in a `llvm::call_once` inside
  `Context` construction rather than requiring callers to do it, keeping the
  "no global mutable state *of FeMe's own*" property honest about the one
  piece of LLVM that genuinely is global.
- **Runtime symbols**: the runtime support library's helpers are resolved
  from an explicitly populated symbol map, not from the host process's
  dynamic symbol table — an embedded driver must not have shader code
  reaching arbitrary host symbols.
- **Dispatch parallelism**: `dispatch()` runs groups across an
  `llvm::ThreadPool` owned by the `Context` (or the calling thread when the
  pool has one thread). Groups are independent by definition, so this needs
  no synchronization beyond the join.
- **Caching**: an `ObjectCache` can be attached so a host can persist
  compiled shaders; the cache key must include the wave size, opt level and
  robustness setting, not just the input hash.
- **Object-file path**: the same pipeline minus the JIT, through
  `feme::TargetMachineBackend` with the host triple, producing a relocatable
  object with the same ABI. This is what makes `--target=<host-triple>` work
  as an ordinary FeMe target and gives an AOT story for free.

## Runtime Support Library

A small static library (`libFeMeRuntimeCPU`) with a C ABI, containing only
what cannot reasonably be emitted as IR:

- Typed-buffer format pack/unpack for the formats that are not a direct
  bitcast (UNORM/SNORM, packed 10:10:10:2, 16-bit float, sRGB).
- Atomic helpers for formats needing read-modify-write conversion.
- The host-side dispatch loop, so the object-file path has a usable
  `main`-adjacent entry point without every embedder rewriting it.

It deliberately does **not** contain a math library: `llvm.sin` and friends
lower through the host's normal vector-math handling.

## Tooling and Testing

### Command line

- `feme --target=<host-triple> --wave-size=N` produces an object file.
  `--wave-size` is a new `DriverOptions` field, defaulting to the host's
  natural width, and is ignored (with a diagnostic if explicitly set) for
  non-CPU targets.
- `feme-opt` gains one pass name per phase, matching the existing
  convention: `feme-cpu-prepare`, `feme-cpu-lower-resources`,
  `feme-cpu-linearize`, `feme-cpu-simdize`, `feme-cpu-lower-wave`,
  `feme-cpu-wrap-entry`, plus the `print<feme-cpu-uniformity>` printer.
- **`feme-run`** (new): JITs a DXIL/SPIR-V/LLVM IR input and dispatches it,
  with resources described by a small YAML file (buffer contents in, buffer
  contents out, as text). This is the tool that turns "does this translate
  correctly?" into "does this compute the right answer?" in `lit`:

  ```yaml
  # feme-run --wave-size=8 --groups=4,1,1 shader.dxil --bind=bind.yaml
  resources:
    - space: 0
      register: 0
      kind: typed-buffer
      format: r32g32b32a32_float
      data: [0.0, 1.0, 2.0, 3.0, ...]
  ```

  and the output buffer is printed for `FileCheck` to match. Deliberately
  textual, per Design.md's "Avoiding binary test fixtures" section.

### Test strategy per phase

Following the instruction that each phase of translation gets unit tests:

| Phase | Unit tests (`gtest`) | `lit` tests |
|---|---|---|
| Uniformity | divergence classification on hand-built IR, including sync dependence | `print<feme-cpu-uniformity>` output |
| Prepare | pass ordering/entry selection | structurization of an unstructured DXIL-derived CFG |
| Resource lowering | slot assignment order, binding table extraction | one test per resource kind, plus binding-array indexing |
| Linearize | mask construction on diamond/loop CFGs | per-CFG-shape `CHECK`s, uniform-branch preservation |
| SIMDize | widening rules, contiguity detection | per-construct `CHECK`s at `W` ∈ {1, 4, 8} |
| Wave lowering | one test per intrinsic | per-intrinsic `CHECK`s at two wave sizes |
| Entry wrapper | barrier region splitting | wave loop shape, barrier split, groupshared |
| JIT | `JITEngine::create`/`dispatch` on a tiny module, binding table round-trip | — |
| End to end | — | `feme-run` executing real shaders and `FileCheck`ing results, at several wave sizes |

The `W = 1` configuration is worth calling out as a testing tool in its own
right: it exercises the entire pipeline while producing scalar code, so a
mismatch between `W = 1` and `W = 8` results for the same shader isolates a
widening bug from a translation bug. Differential testing between wave sizes
is the cheapest high-value test this design enables, and should be a
first-class part of the test suite rather than an afterthought.

## Directory / Library Layout Additions

Extending the layout in Design.md:

```
feme/
  include/feme/
    Analysis/
      CPU/WaveUniformity.h        (WaveTTIImpl, computeWaveUniformity)
    Transforms/
      CPU/Prepare.h
      CPU/ResourceLowering.h
      CPU/Linearize.h
      CPU/SIMDize.h
      CPU/WaveLowering.h
      CPU/EntryWrapper.h
    Target/
      CPU/RuntimeABI.h            (FemeDescriptor, FemeDispatchArgs; C ABI)
      CPU/BindingTable.h
      CPU/JITEngine.h
  lib/
    Analysis/CPU/...
    Transforms/CPU/...
    Target/CPU/...
  runtime/
    CPU/                          (libFeMeRuntimeCPU, C ABI)
  tools/
    feme-run/
```

`Analysis/` is a new top-level module; the alternative (putting
`WaveUniformity` under `Transforms/CPU/`) would make an analysis usable by
non-CPU consumers live in a target-specific directory. Nothing else in the
existing layout moves.

## Roadmap / Milestones

Sequenced so each step is independently testable and useful:

1. **Scaffolding + ABI header**: `Target/CPU/RuntimeABI.h`, `--wave-size`
   plumbed through `DriverOptions`, empty passes registered in `feme-opt`.
2. **Uniformity analysis** (`WaveTTIImpl` + printer + unit tests). No
   transform yet.
3. **Resource lowering** to the descriptor table, with the binding table
   metadata and reader. Testable at `W`-agnostic scale.
4. **`W = 1` end-to-end**: prepare + trivial linearize + trivial widen +
   entry wrapper, no divergence handling, plus `feme-run` and the JIT. This
   is the first point at which a shader *runs*, and it deliberately comes
   before the hard transform — it makes every subsequent step verifiable by
   execution rather than by IR inspection alone.
5. **Linearization** for divergent control flow (straight-line diamonds,
   then loops).
6. **Widening** for `W > 1`, including masked memory ops and the
   scalarization fallback.
7. **Wave intrinsic lowering**.
8. **Barriers and groupshared memory** (region splitting).
9. **Format conversion runtime** for non-trivial typed buffer formats.
10. **Performance work**: contiguity detection, all-lanes-off branch
    skipping, uniform-load hoisting. Only after correctness is established
    and measurable.

## Open Questions

These need answers (or at least preferences) to firm this design up:

1. **Wave size default and range.** Is a host-derived default (`W` =
   host vector width / 32 bits) right, or should `W` always be explicit? And
   is 64/128 worth supporting on a 128-bit-vector host, or should the
   supported set be bounded by what the host can do without pathological
   legalization?
2. **Required wave size conflicts.** When a shader declares
   `[WaveSize(32)]` and the user asks for `W = 8`: hard error, or warn and
   honour the shader? (This design assumes hard error.)
3. **Descriptor table vs. kernel arguments.** Is the flat descriptor table
   the right host-facing model, or should the CPU target match a specific
   API's binding model (D3D12 root signatures, Vulkan descriptor sets) more
   closely? The proposal here is deliberately the lowest common denominator
   — is there a host already in mind whose model this should match?
4. **Robustness by default.** Bounds-checked, zero-returning OOB access is
   proposed as the default. Is a "trust the shader" mode needed at all, and
   if so should it be per-resource rather than global?
5. **JIT scope.** Is `feme::cpu::JITEngine` owning its own dispatch (thread
   pool, group loop) the right shape, or should FeMe only hand back a
   function pointer plus the ABI description and let the host schedule?
   The former is much better for testing; the latter is what a real driver
   would want. (Both are possible; the question is which is v1.)
6. **Graphics stages.** Is compute-only acceptable indefinitely, or is
   there a pixel/vertex shader use case that should shape the design now
   (in particular the lane-to-quad mapping and the wrapper's shape)?
7. **Mask representation between phases.** Phase 3 needs to hand Phase 4 a
   per-instruction mask. Operand bundles on the instruction, a FeMe-internal
   intrinsic taking a mask token, or an out-of-IR side table computed on
   demand? Bundles survive printing (good for testing) but are unusual on
   non-call instructions; a side table keeps the IR clean but makes the
   phases untestable in isolation, which conflicts with this design's
   testing goals. Current preference: a FeMe-internal
   `llvm.feme.cpu.masked.*` intrinsic form for the operations that need it,
   so the intermediate IR is printable and checkable.
8. **Where does SPIR-V's structurization leave us?** DXIL input can be
   arbitrarily unstructured; `StructurizeCFG` handles reducible CFGs and
   `FixIrreducible` the rest, but the combination is not commonly exercised
   on shader-shaped code. Is it acceptable for v1 to reject the cases those
   passes handle badly, or must every DXIL input work?
9. **Relationship to MLIR.** This design operates entirely on `llvm::Module`
   (consistent with the existing AMDGPU/SPIR-V lowering passes). Is there
   interest in doing the SIMD-ization at the MLIR level instead (e.g. via
   the `vector` dialect), which would be more expressive but would mean
   SPIR-V and DXIL inputs converge much later?
