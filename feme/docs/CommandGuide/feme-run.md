# `feme-run` — FeMe CPU target JIT/dispatch runner

## SYNOPSIS

```shell
feme-run [options] <input .ll/.bc file, or DXIL bitcode/DXContainer>
```

## DESCRIPTION

`feme-run` JITs a raised shader through `feme::cpu::JITEngine` and
dispatches it against a small, textual heap description, printing the
resulting heap contents. This is the tool that turns "does this translate
correctly?" into "does this compute the right answer?" for `lit` tests (see
the "Command line" section of [../FeMeCPUDesign.md](../FeMeCPUDesign.md)):
a test can assert on what a shader actually computed, not only on the shape
of its IR.

**Scope (roadmap milestone 4, updated by milestones 10 and 11):** the input
may either already be idiomatic, raised LLVM IR (`.ll`/`.bc`), or a DXIL
bitcode file/DXContainer -- `feme-run` sniffs which, and for DXIL runs the
same import + op/metadata raising `feme::Driver` runs before any
target-specific lowering. SPIR-V import remains unwired (see
feme/docs/FeMeCPUDesign.md's Status section for why). Every resource-heap
descriptor this tool's YAML format describes is an untyped byte buffer (a
raw/structured buffer, never a typed-buffer format conversion), matching
what `libFeMeRuntimeCPU` and the CPU pipeline's resource-call
scalarization exercise today.

Native opcode 140 and legacy opcode 69 raw/structured stores accept
contiguous masks 3/7/15 for packed two/three/four-component i32/FP32
values. These use existing vector helpers, preserving full-width bounds:
partly out-of-bounds stores commit no words; three components write
12 bytes, not a padded 16-byte value. Scalar behavior remains; sparse/
dynamic/oversized masks, packed non-32-bit types and mixed active fields
stay unraised. Native SDK DXC SM6.6 heap and SM6.0 bound compute tests
cover normal waves 4/8, reference and ARM64 AOT-object execution, exact
bits, field strides/sub-offsets, divergent masks, five lanes, per-width/
per-record guards and two checked repeats. Legacy AOT supplies an explicit
physical `resource-heap`, not `bindings` YAML. Runtime/SPIR-V implementation,
status handling, artifact ABI 8 and argument layouts are unchanged;
this is CPU execution, not owned D3D12.

Packed i32/FP32 raw/structured loads likewise use the existing vector
helpers. Opcode 139 contiguous masks 1/3/7/15 retain explicit widths for
partial result uses; legacy opcode 68 requires complete leading-component
uses to infer width. Scalar support remains. Invalid masks/types,
sparse/isolated-later legacy uses, status/aggregate uses and unused loads
stay unraised. Native SM6.6/SM6.0 `Load2`/`Load3`/`Load4` and structured
uint4/float3 tests verify exact bits, five lanes, divergent uses, every
word of a guarded 784-byte output, last-valid vectors, all-zero partial/
large-address reads and two repeats in normal waves 4/8, reference and
ARM64 AOT modes. A separate tail descriptor avoids legacy source-width
coalescing; general legacy partial/coalesced-width semantics are unproven.
Legacy AOT still supplies a physical heap; no runtime/SPIR-V, status,
ABI or owned-device change is implied.

Native FP32 structured-buffer tests additionally execute float2/float3/
float4 loads and stores at offset 4 with strides 12/16/20 across the same
SM6.6/SM6.0 CPU modes. Exact copies preserve negative zero, signed
subnormals/infinities/quiet-NaN payloads and adjacent values near one
without arithmetic. Truncated final vectors and invalid indices read all
zero; partial/invalid writes leave inputs unchanged. Five lanes, divergent
copies, fifteen output records per width, every word of 732 output bytes,
record/trailing guards and two repeats pass, including 12-byte float3
stores. This adds copy/bounds evidence, not wider legacy admission,
implementation changes or general FP32 arithmetic/conformance.

Native raw/structured i32 atomics additionally cover add/and/or/xor,
signed/unsigned min/max, exchange and successful/failed compare-exchange
in SM6.6 heap and SM6.0 bound shaders across the same CPU modes. Existing
getpointer/atomicrmw/cmpxchg and CPU helpers preserve stride 12 and field
offsets 4/8. Five independently initialized lanes check exact old values,
high bits, UINT_MAX + 1 wraparound, divergent increments, invalid-address
zero/no-op behavior and all words of two guarded 64-byte state buffers
and a guarded 744-byte output twice. Legacy AOT takes a physical heap.
This is not contention/memory-order/barrier conformance, typed/image/int64
atomic breadth, new descriptor materialization or an implementation/ABI
change.

Native typed R32_SINT/R32_UINT atomics additionally check all nine binary
operations across the two formats, successful/failed compare-exchange,
exact old values, wraparound, divergent increments and element-index
bounds across the same CPU modes. Modern scalar and legacy atomic-only
four-component fallback handles retain signedness. Atomic exchange
initializes five lane-owned element pairs; all words of two guarded
28-byte state buffers and a guarded 464-byte output pass twice.
This does not establish new legacy width inference, contention/order/
barrier, other-format/image/int64 atomic or ABI changes.

Full-mask typed-store admission now closes that probe's scalar-store gap:
opcode 69 mask-15 i32/FP32 payloads through narrower handles retain all
four channels in canonical vector stores, with declared width/signedness
unchanged. Existing vector helpers honor the bound format instead of
filling other channels 0/0/1 through scalar helpers. Native SM6.6/SM6.0
scalar SINT/UINT/FP32 assignments pass exact-output R32/RGBA32 cases
across the same CPU modes, checking all broadcast channels, special bits,
divergent stores, invalid indices, complete texel guards and two repeats.
Invalid/nonmatching partial masks, mixed types and non-32-bit promotion
stay unraised. This is bounded store evidence, not general typed-load/
store or owned-device closure; runtime/SPIR-V code, loads/status, atomics
and ABI remain unchanged.

Native four-component typed loads additionally cover SDK DXC SM6.6/SM6.0
SINT/UINT/FP32 SRV/UAV shaders on nine R32/RGB32/RGBA32 formats across
the same CPU modes. Exact bits, valid R32 zero-G/B/one-alpha defaults,
RGB32's real 12-byte stride and alpha one versus all-zero incomplete/
invalid loads, valid-end accesses, divergent use, complete input/
output words, guards and two repeats pass. Distinct inputs prove namespace
separation; legacy AOT requires explicit physical heaps in space/register/
namespace order, interleaving SRVs and UAVs at equal registers. Object
execution does not materialize logical binding YAML. This is bounded CPU
evidence, not new admission, status, runtime/SPIR-V, ABI or owned-device
behavior.

Native scalar/vector-two typed loads now cover SDK DXC SM6.6/SM6.0
SINT/UINT/FP32 SRV/UAV shaders on matched R32/RG32 formats across the
same CPU modes, now also on wider RGB32 descriptors at both widths.
Narrow handles/helpers preserve exact bits and RGB32's physical 12-byte
stride. Incomplete RG32/RGB32 elements return zero; RGB32 missing Z is
invalid even when R/G fit. Upper/negative indices return zero at both
widths. Namespace separation, valid-end accesses, divergence,
all input/output words, guards and two repeats pass. Legacy AOT uses
explicit space/register/namespace-ordered physical heaps. This adds narrow
CPU evidence, not new admission, status, runtime/SPIR-V, ABI or general
typed/owned-device closure.

The shared RGB32 scalar/vector-two layouts compare six 56-byte inputs and
a guarded 296-byte raw output, exactly 632 bytes per case. Scalar execution
also checks its 37-word trailing sentinel region remains 42; vector-two
execution checks the complete output. Both repeats are checked.

Native typed-buffer `GetDimensions` now executes SDK DXC SM6.6/SM6.0
SINT/UINT/FP32 SRV/UAV queries on RGB32 and mixed R32/RG32/RGBA32
layouts across the same CPU modes. Field-zero queries with unused/zero
mip operands raise/lower to the existing typed element-count helper;
other fields, aggregate uses and meaningful mips remain outside this path.
Physical format/byte size determine
complete-element counts, including partial/too-short views. Namespace
separation, divergence, all input/output words, guards and repeats pass:
224 input bytes plus a guarded 128-byte output, exactly 352 bytes per case.
Legacy AOT uses physical heaps, not logical binding YAML. Runtime/SPIR-V
code, ABI 8 and argument layouts remain unchanged; this is CPU evidence,
not new stores, advertisements or owned-device closure.

Native `ByteAddressBuffer`/`RWByteAddressBuffer.GetDimensions` now
executes SDK DXC SM6.6/SM6.0 SRV/UAV queries across the same CPU modes.
Field-zero queries with unused/zero mip operands raise/lower to the raw
metadata helper using stride one/prefix zero. Sizes 20/28/4/88 return
those byte counts, not 5/7/1/22 DWORD counts. Namespace separation,
divergence, query/store use of the output handle, every word, guards and
repeats pass: 52 input bytes plus a guarded 88-byte output, exactly 140
bytes per case. Heap YAML uses `kind: raw-buffer`; legacy AOT uses physical
heaps. Buffer xy fields remain unsupported.
Runtime/SPIR-V code, ABI 8 and argument layouts remain unchanged.

Native `StructuredBuffer`/`RWStructuredBuffer.GetDimensions` now also
executes SDK DXC SM6.6/SM6.0 SRV/UAV queries across the same CPU modes.
Field-zero counts lower to the raw metadata helper using the handle's
declared structure size as stride, prefix zero; DXC's constant HLSL stride
output is preserved. Ranges of 32/64/8 bytes and strides 12/20/12 yield
counts 2/3/0, excluding incomplete elements while still reporting declared
stride for the too-short bound view. Namespaces, divergence, every word,
guards and repeats pass: 104 input bytes plus a guarded 128-byte output,
exactly 232 bytes per case. Heap YAML uses `kind: structured-buffer` and
matching `stride`; legacy AOT uses physical heaps. Buffer xy fields,
aggregate uses and meaningful mips remain unsupported. Runtime/SPIR-V
code, ABI 8 and argument layouts remain unchanged; no owned-device closure.

Native heap-backed `Texture2D`/`RWTexture2D.GetDimensions` now executes
SDK DXC SM6.6 FP32/i32 scalar/pair queries in waves 4/8, reference and
ARM64 AOT. YAML `images` supplies the descriptors; no sampler is needed.
The existing CPU metadata helper returns width/height at mip zero.
Width-only, height-only and paired outputs distinguish extents 5x3, 3x2
and 2x4. The full image has three mips and still reports its base extent;
the same handle also loads a texel. Divergent unbound indices return zeros;
sentinels, every word, guards and repeats pass: 128 input bytes plus a
guarded 108-byte output, exactly 236 bytes per case. Non-32-bit
declarations, aggregate uses and bound/static
images remain outside this CPU path. Sampled plain 2D explicit mips/counts
use the separate path below; unsupported canonical query shapes
fail early normal/reference/AOT admission. Runtime/SPIR-V code, buffer
dimensions, ABI 8 and argument layouts remain unchanged; no owned-device
or conformance closure is added.

Native heap-backed 1D/array dimensions now also execute SDK DXC SM6.6
FP32/i32 SRV/UAV queries in waves 4/8, reference and ARM64 AOT. YAML
`images` uses `dimension: 1d`/`1d-array`, one-component `extent` and
`array-layers` for arrays. No sampler is needed. Existing metadata helpers
receive literal mip zero. Plain 1D accepts scalar width only; array 1D
accepts scalar width or width/layer count. Three-mip plain/array SRVs
report base widths 5/7 and array layers 2; UAVs report width 3 and
width 2/layers 3. Divergent/unbound indices, alternating-layer exact
integer loads through a queried handle, sentinels, every word, guards
and repeats pass: 156 input bytes plus a guarded 148-byte output,
exactly 304 bytes per case. Plain 1D base-pair queries remain rejected;
explicit queries use the paths below. Runtime/
SPIR-V code, ABI 8 and layouts remain unchanged. Other shapes/fields,
bound/static images and owned-device/conformance closure remain open.

Native heap-backed sampled `Texture2D.GetDimensions(mip, width, height,
levels)` now executes SDK DXC SM6.6 FP32/i32 queries in waves 4/8,
reference and ARM64 AOT. YAML `images` supplies real mip chains;
no sampler is needed. Existing CPU size/level metadata helpers preserve
the live mip and fixed total count. Native extents 7x3/3x1/1x1 and
5x2/2x1/1x1 distinguish three valid mips, with divergent/unbound
indices, query-derived exact SINT loads, all words, guards and repeats:
152 input bytes plus a guarded 128-byte output, exactly 280 bytes.
The prior count-unused explicit-mip variant now also passes four CPU
modes, 236 bytes per case. Both variants repeat twice. Writable/unsupported-
shape/undefined-mip canonical forms reject; depth/aggregate uses remain
unraised. Shared helper clamping is unchanged; native out-of-range-mip
semantics are not claimed. Runtime/SPIR-V code, ABI 8 and layouts remain
unchanged; no bound/static images or owned-device closure is added.

Native heap-backed sampled `Texture1DArray.GetDimensions(mip, width,
layers, levels)` now executes SDK DXC SM6.6 FP32/i32 queries in the same
four CPU modes, each repeated twice. YAML uses `dimension: 1d-array`,
one-component `extent`, `array-layers` and `mip-levels`; data is mip-major,
with all layers contiguous within each mip. Existing size/level metadata
helpers preserve the live mip, fixed layers and total mip count without
samplers or derivatives. Widths 7/3/1 with two layers/three mips and
widths 5/2 with three layers/two mips distinguish the components.
Query-derived last-X/last-layer exact SINT loads, divergent/unbound
indices, skipped-store sentinels, every word, guards and repeats pass:
172 input bytes plus a guarded 148-byte output, exactly 320 compared
bytes per case. The prior count-unused 1D-array variant also executes
four modes, repeated twice, checking 304 bytes each. Writable explicit
queries remain unsupported. Only valid mips are measured;
helper clamping, runtime/SPIR-V code, ABI 8 and layouts are unchanged.
No bound/static images or owned-device/conformance closure is added.

Native heap-backed sampled `Texture1D.GetDimensions(mip, width, levels)`
now executes FP32/i32 SDK DXC SM6.6 queries in waves 4/8, reference
and ARM64 AOT, each repeated twice. YAML uses `dimension: 1d`, a
one-component `extent` and `mip-levels`, with mip-major data. Widths
7/3/1 with three mips and widths 5/2 with two mips distinguish live
mips and fixed total counts. Query-derived last-X exact SINT loads,
divergent/unbound indices, skipped-store sentinels, every word, guards
and repeats pass: 72 input bytes plus a guarded 108-byte output,
exactly 180 compared bytes per case. The prior count-unused plain-1D
variant also executes four modes, repeated twice, checking 304 bytes
each. No sampler or derivatives are needed. Writable/undefined-mip and
mismatched-shape canonical queries reject; base 1D pairs remain unsupported.
Only valid native mips are measured. Helper clamping, runtime/SPIR-V
code, ABI 8, layouts and owned-device boundaries remain unchanged.

Native heap-backed sampled `TextureCube.GetDimensions` now executes
FP32/i32 base width/height and explicit `(mip, width, height, levels)`
queries in waves 4/8, reference and ARM64 AOT, each repeated twice.
YAML uses `dimension: cube`, a square two-component `extent` and
`mip-levels`; the runner supplies six faces, mip-major. Queries reuse
existing metadata helpers without sampler/derivatives. The query-only
variant has no sampler heap; the sampling variant uses queried width/count
to select +X and reverse the three real mips. Base 4x4 versus valid
4x4/2x2/1x1, integer cube 2x2/1x1 with two mips, divergent/unbound
indices, skipped-store sentinels, every word, guards and repeats pass.
The query-only variant checks 624 image bytes plus a guarded 168-byte
output, exactly 792 compared bytes; sampling additionally checks all
80 sampler bytes, exactly 872 bytes per case. Writable cubes, cube
arrays and undefined-mip canonical forms reject. Only valid native mips
are measured. Helper clamping, runtime/SPIR-V code, ABI 8, layouts and
owned-device/conformance boundaries remain unchanged.

Native heap-backed `Texture2DArray`/`RWTexture2DArray.GetDimensions` now
executes FP32/i32 base width/height/layers queries and sampled explicit
`(mip, width, height, layers, levels)` queries in waves 4/8, reference
and ARM64 AOT, each repeated twice. YAML uses `dimension: 2d-array`,
two-component `extent`, `array-layers` and `mip-levels`; data is mip-major
with layers contiguous within each mip. Existing CPU metadata helpers
need no sampler/derivatives. Base 7x3/layers 2, valid 7x3/3x1/1x1 with
three mips, SINT 5x2/2x1 with three layers/two mips and UINT UAV
2x1/layers 4 distinguish extents, layers and counts. Query-derived
last-X/Y/layer exact SINT loads, divergent/unbound indices, skipped-store
sentinels, every word, guards and repeats pass: 376 image bytes plus a
guarded 308-byte output, exactly 684 compared bytes per case. Writable
explicit, undefined-mip and mismatched-shape canonical queries reject;
MS dimensions use the separate path below. Only valid native mips
are measured. Helper clamping, runtime/SPIR-V code, ABI 8, layouts and
owned-device/conformance boundaries remain unchanged.

Native heap-backed `Texture3D`/`RWTexture3D.GetDimensions` executes FP32/i32
base width/height/depth queries and sampled explicit
`(mip, width, height, depth, levels)` queries in waves 4/8, reference and
ARM64 AOT, each repeated twice. YAML uses `dimension: 3d`, three-component
`extent` and `mip-levels`; data is mip-major with depth slices contiguous
within each mip. Existing metadata helpers need no sampler/derivatives.
FP32 extents 7x3x5/3x1x2/1x1x1 with three mips, SINT 5x2x3/2x1x1
with two mips and UINT UAV 2x1x4 distinguish shrinking depth from total
mip count and fixed array layers. Query-derived last-X/Y/Z exact SINT
loads, divergence/unbound zeros, skipped stores, every word, guards and
repeats pass: 608 image bytes plus a guarded 308-byte output, exactly
916 compared bytes per case. Writable explicit, undefined-mip and wrong
result-form queries reject. Only valid native mips are measured; runtime/
SPIR-V code, helper clamping, ABI 8 and owned-device boundaries are unchanged.

Native heap-backed sampled `TextureCubeArray.GetDimensions` executes
FP32/i32 base width/height/cube-count and explicit
`(mip, width, height, cubes, levels)` queries in waves 4/8, reference and
ARM64 AOT, each repeated twice. YAML uses `dimension: cube-array`,
two-component `extent`, `mip-levels` and physical `array-layers` (a
positive multiple of six); data is mip-major with all faces contiguous
within each mip. Eighteen/twelve physical faces report three/two cubes,
independently of two/one total mip counts. Metadata queries need no
sampler/derivatives. A query-only variant has no sampler heap; a sampling
variant uses queried counts to select the last cube's +X face and reverse
two real mips. Divergence/unbound zeros, skipped stores, every word, guards
and repeats pass: 408 image bytes plus a guarded 248-byte output, exactly
656 compared bytes; sampling also checks all 80 sampler bytes, totaling
736 bytes per case. Writable cube arrays, undefined mips and wrong result
forms reject. Only valid native mips are measured; runtime/SPIR-V code,
helper clamping, ABI 8 and owned-device boundaries remain unchanged.

Native heap-backed sampled FP32/i32 `Texture2DMS`/`Texture2DMSArray`
dimensions return width/height, optional fixed layers and sample count,
without a mip operand, sampler or derivatives. YAML uses `dimension:
2d-ms`/`2d-ms-array`, two-component `extent`, `sample-count` and physical
`array-layers` for arrays; MS images retain one mip. Sample values are
contiguous per texel and layers are contiguous. Native SM6.6 passes waves
4/8, reference and ARM64 AOT, each repeated twice. FP32 3x2 with four
samples and SINT 2x1/layers three with two samples distinguish extents,
layers and sample counts. Query-derived last-X/Y/layer and sample indices
load exact FP32/SINT values. Divergence/unbound zeros, skipped stores,
every word, guards and repeats pass: 144 image bytes plus a guarded
188-byte output, exactly 332 compared bytes per case. Writable/non-heap/
non-32-bit, wrong result forms, samples-on-non-MS and levels-on-MS queries
reject. Runtime/SPIR-V code, existing load/store/status/atomic policy,
ABI 8 and owned-device boundaries remain unchanged; resolve is not added.

Native float-output `GetDimensions` overloads also have
[`native-float-texture-dimensions.test`](../../test/Tools/feme-run/HLSL/native-float-texture-dimensions.test)
coverage for plain 2D base/explicit-mip, cube-array mip/cube/level and
MS-array extent/layer/sample queries. The same four CPU modes each
repeat twice, checking every word and guard across 436 image bytes and
a 268-byte output, exactly 704 compared bytes per case. The tested SDK
DXC emits `sitofp`, which raising preserves. Separate portable/JIT tests
distinguish signed and unsigned FP32 conversions at synthetic large
metadata widths; they do not imply native D3D resource-size support.
This adds no CLI, resource binding, runtime/ABI or owned-device capability.

Three-component typed i32/FP32 loads now lower through existing vector-four
helpers followed by XYZ selection. Physical formats retain stride and
complete-texel bounds: RGB32 uses 12 bytes; an RGBA32 texel missing W
is invalid even when XYZ fits. Portable i32/FP32 regressions and native
SM6.6/SM6.0 FP32 SRV/UAV RGB32/RGBA32 cases check W discard,
incomplete/invalid all-zero reads, exact bits, namespace separation, valid-
end loads, divergence, guards and two repeats across the same CPU modes.
This replaces nonexistent typed `v3` calls without runtime/SPIR-V, store,
status, ABI or owned-device changes; general format closure is not claimed.

Native integer-three typed loads now execute SDK DXC SM6.6/SM6.0 `int3`/
`uint3` SRV/UAV shaders on RGB32/RGBA32 SINT/UINT across the same CPU
modes. RGB32 integer reads now use exact 12-byte texels and alpha one;
existing vector-four helpers select XYZ. Missing RGB32 Z/RGBA32 W and
upper/negative indices read all zero. Exact bits, namespace separation,
divergence, every input/output word, guards and two repeats pass: 520 RGB
or 600 RGBA bytes per case, including a guarded 296-byte output. Legacy
AOT uses space/register/namespace-ordered physical heaps. Runtime tests
also cover narrow/wide typed reads, dimensions, integer image loads and
fixed-alpha borders. RGB32 integer typed stores remain tested no-ops;
advertisements, raising/lowering, SPIR-V code, ABI 8 and argument layouts
are unchanged. General typed-format and owned-device closure remain open.

Like `feme-opt`, `feme-run` is a testing-oriented tool and may use
`llvm::cl::opt` freely.

## OPTIONS

- `--wave-size=<N>`: the wave size to compile at. `0` (the default)
  resolves it from the shader's own declaration or the host, per "Wave
  Size Selection" in [../FeMeCPUDesign.md](../FeMeCPUDesign.md).
- `--groups=<X,Y,Z>`: the dispatch's group count (default `1,1,1`).
- `--entry-point=<name>`: the compute entry point to run, if the module has
  more than one.
- `--reference`: runs the shader one invocation at a time through the
  unwidened module, instead of Phases 3/4 (linearization and widening) --
  the ground truth the CFG restructurization test suite (roadmap milestone
  5, see [../FeMeCPUDesign.md](../FeMeCPUDesign.md)) diffs against.
  `--wave-size` is ignored. A shader using a wave intrinsic (which has no
  meaning one invocation at a time) is rejected.
- `--object`: loads a native object compiled by
  `feme --target=<host-triple>` and dispatches its
  `feme_cpu_entry_<entry-point>` symbol instead of compiling input IR.
  Traditional `bindings`, `image-bindings` and `sampler-bindings` are
  rejected because compiled objects retain no binding-prefix reflection.
  Logical heap entries and YAML `root-constants` are supported.
- `--root-signature-constants`: opts into the checked root signature's sole
  visible inline-constant parameter instead of inferring a cbuffer binding.
  Its DWORD count determines the required root span and clips shader reads,
  even when extra bytes are supplied. Reflection retains its full span and
  register/space when no cbuffer reads survive; unused parameters do not
  become zero-sized inferred bindings. Missing/invalid signatures, multiple
  visible parameters, local signatures and stage denial are errors.
  Before normalization, live bound-resource declarations in the entry and
  its directly called helpers must match the root plan's resource class,
  register space and stage visibility. One root binding must contain each
  complete declared range; adjacent ranges or individual static samplers
  cannot collectively cover an array. Dynamic declarations and indirect
  calls fail validation. Root SRV/UAV descriptors require raw or structured
  buffers; typed buffers and textures require descriptor tables. Live
  original native heap handles require `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`
  for resources or `SAMPLER_HEAP_DIRECTLY_INDEXED` for samplers. Heap
  accesses generated for traditional bound emulation do not need these
  flags. Visible static sampler filters must match the declared default/
  comparison mode. Minimum/maximum filters are non-comparison filters;
  mismatches fail with register/space and function-specific diagnostics.
  Descriptor-table/native-heap sampler contents, counter compatibility
  and descriptor promises are not validated by this check.
  Legacy DXC sampler handles now preserve default/comparison modes, so real
  native shaders reach this check. Valid and inferred sampling remains
  unsupported at bound-descriptor/comparison-access admission; this does
  not materialize or execute static samplers.
  Scalar FP32 `SampleCmpLevelZero` now has checked opcode-65 raising, so
  valid native bound access reaches the descriptor error. Canonical
  heap-backed scalar FP32 1D/2D/cube comparison, including array forms, now
  executes in normal/reference/AOT modes. Plain 1D uses scalar FP32 U
  and a constant scalar i32 offset; 1D-array coordinates are FP32 (U, layer).
  Plain/array 2D retains two/three FP32 coordinates and two constant i32
  spatial offsets. Supply image and comparison-sampler entries in the YAML
  heaps; point/linear comparison filtering uses the shared runtime.
  Array layers are separate from spatial offsets; omitting the layer
  fails explicit admission.
  Cube coordinates are FP32 direction vectors of width three, with a
  fourth cube-element coordinate for arrays. Unused scalar i32 offsets
  accept poison/undef or zero; nonzero/dynamic offsets fail admission.
  Other dimensions and dynamic/undefined offsets fail explicit admission.
  Scalar FP32 implicit `SampleCmp` now has checked opcode-64 import/raising,
  with undefined/poison clamps selecting `llvm.dx.resource.samplecmp` and
  real zero/nonzero/dynamic clamps preserved in `samplecmp.clamp`.
  Heap-backed scalar FP32 1D/2D/cube implicit comparison, including arrays, executes in
  the separate compiled fragment-stage path, with real coarse quad
  spatial/direction derivatives, implicit LOD, supported offsets and the optional
  shader MinLod clamp. This compute runner still rejects implicit comparison
  in normal/reference/AOT dispatch. Other shapes/stages,
  dynamic/undefined spatial offsets and half/vector results remain unsupported.
  2D-array coordinates are FP32 (U, V, layer), with only U/V differentiated
  and two spatial offsets. Plain 1D uses scalar FP32 U and a constant
  scalar i32 offset; 1D arrays use FP32 (U, layer) with the same offset.
  Only U is differentiated. Missing-layer coordinates reject explicitly.
  Compiled fragment array execution checks divergent layers/references,
  dynamic clamps, mip selection, helper lanes and guarded partial waves.
  Cubes differentiate all three raw direction components, not the fourth
  cube-array element coordinate. Unused scalar offsets accept poison/undef
  or zero only. Compiled cube/array tests check all six faces, both elements,
  both derivative axes and dynamic clamps. Native SDK DXC plain/clamped
  cube/array import is not bound-descriptor or owned-device execution.
  FP32 `SampleBias` also has checked opcode-61 import/raising for 1D/array,
  2D/array, 3D and cube/array, with scalar and two-/three-/four-component
  FP32 texels and default samplers. Bias, spatial offsets and optional real
  MinLod clamps survive. Undefined/poison clamps select the plain intrinsic;
  real zero/nonzero/dynamic clamps remain clamped. Seven-shape LLVM roundtrips
  and native DXC pixel-shader import are tested.
  Heap-backed FP32 1D/2D/3D/cube biased sampling, including arrays, now executes
  in the separate compiled fragment-stage path, with real spatial/direction
  derivatives, implicit LOD, bias, supported offsets and optional shader MinLod clamps.
  Scalar/vector results up to the declared texel width (at most four) narrow
  the shared four-component result. Mip/offset/component selection,
  divergent bias/clamps, helpers and guarded partial waves are tested.
  This compute runner still rejects both biased intrinsics in normal/
  reference/AOT dispatch. Other shapes/stages, non-heap handles, comparison
  samplers and dynamic/undefined spatial offsets also reject explicitly; no
  descriptor materialization or owned-device execution is enabled.
  Biased 2D arrays use FP32 (U, V, layer); only U/V are differentiated and
  the two offsets remain spatial. Compiled array tests check divergent
  layers/bias/clamps, layer/mip/offset/component selection and guarded
  partial waves. Missing-layer coordinates still reject explicitly.
  Cubes use six raw-XYZ derivatives for face-projected implicit LOD; a
  separate cube-array element is never differentiated. Unused scalar offsets
  must be poison/undef or zero. Compiled tests check all faces, both elements,
  biased/clamped mip selection and components with guarded partial waves.
  Nonzero/dynamic cube offsets remain unsupported.
  Biased 1D sampling uses scalar U and a constant signed scalar i32 offset;
  arrays use (U, layer), differentiating only U and keeping offsets spatial.
  Compiled tests check mip-0/mip-1 footprints, both derivative axes,
  layer/mip/offset/component selection, divergent layers/bias/clamps and
  guarded partial waves. This compute runner still rejects both forms.
  Malformed coordinates and dynamic/poison/undefined 1D offsets reject.
  Implicit DXIL 1D sampling outside fragment stages and non-32-bit 1D/array loading remain
  unsupported.
  Biased 3D sampling uses FP32 (U, V, W), six coarse spatial derivatives and
  three constant signed i32 offsets. Compiled tests isolate U/V/W LOD on
  both screen axes, signed XYZ offsets, biased/clamped mips and components
  with guarded partial waves. Malformed coordinates/offset widths and
  dynamic or partially undefined offsets reject. This compute runner still
  rejects plain/clamped 3D bias in normal/reference/AOT dispatch. Implicit
  DXIL 3D sampling outside fragment stages and non-32-bit 3D loading remain unsupported;
  no descriptor materialization or owned-device execution is enabled.
  Heap-backed FP32 cube/array `SampleLevel` now executes through shared
  checked lowering with default samplers, FP32 direction/array-element
  coordinates, FP32 LOD and result widths one through four up to the
  declared texel width. Cubes retain an unused scalar zero/undef/poison
  offset. Live explicit LOD, six zero derivatives, zero bias, separate
  elements and no shader MinLod clamp reach the existing helpers.
  All `SampleLevel` calls now avoid the legacy implicit-only fallback;
  malformed calls reject in normal/reference/AOT compilation.
  Native DXC cube/plain-array tests execute two real mips with exact
  FP32-bit half-mip blends, negative/upper LOD, sampler MinLod=MaxLod=1,
  all faces/elements, waves 4/8, thirteen lanes, guards and repeat 2 in
  all three paths. Compiled fragment tests add three mips, all widths,
  helpers and masks. Other sampling, runtime/SPIR-V lowering, ABI 8
  and argument layouts are unchanged; owned-device gaps remain open.
  Heap-backed FP32 2D/array `SampleLevel` now executes through the shared
  checked path with default samplers, FP32 U/V or U/V/layer coordinates,
  FP32 LOD, constant signed XY offsets and results up to the declared
  texel width (one through four). Explicit LOD, zero derivatives/bias,
  separate layers and no shader MinLod clamp reach the existing helpers.
  Malformed shapes/offsets, comparison samplers, non-heap handles,
  non-FP32 texels and too-wide results reject in normal/reference/AOT
  admission, including through the old zero-offset fallback. Native
  SDK DXC plain/array fixtures execute three rectangular mips, signed/
  omitted offsets, half-mip blends, negative/upper LOD and sampler min/
  max clamps with waves 4/8, five lanes, guards and repeat 2 through all
  three CPU paths. Compiled fragment tests add all result widths,
  divergent mips/layers/coordinates, helper masks and repeated batches.
  Implicit 2D/cube sampling, runtime/SPIR-V lowering, artifact ABI 8 and
  owned-device gaps are unchanged.
  Heap-backed FP32 1D/array `SampleLevel` now executes in any stage through
  normal/reference/AOT paths with default samplers, scalar U or `(U,layer)`
  coordinates, constant signed scalar offsets and scalar/vector results
  up to the declared texel width (at most four). Explicit LOD is preserved
  with zero derivatives/bias and unchanged sampler LOD behavior.
  Compiled-stage tests verify multi-mip/layer selection and sampler clamps;
  native SDK DXC bindless 1D/array runner tests also execute three real
  mips of widths 5/2/1 with two fixed layers, retaining original one-mip
  coverage. New exact FP32-bit checks cover signed/omitted offsets,
  components/layers, negative/upper LOD, half-mip blends at 0.5/1.5 and
  sampler MinLod=MaxLod=1, with waves 4/8, five lanes, guards and repeat 2.
  Malformed shapes,
  non-heap handles, comparison samplers and dynamic/undefined/poison offsets
  remain rejected. Other ordinary shapes are unchanged; no bound/static
  descriptor materialization or owned
  D3D12 execution is enabled.
  Heap-backed FP32 3D `SampleLevel` now also executes in any stage through
  normal/reference/AOT paths with default samplers, three-component U/V/W
  coordinates, FP32 LOD, constant signed XYZ offsets and scalar/vector
  results up to the declared texel width (at most four). Explicit LOD
  survives with six zero derivatives and zero bias; sampler LOD behavior
  is unchanged. Compiled-stage tests verify multi-mip selection and sampler
  clamps; native SDK DXC bindless 3D runner tests also execute three
  rectangular mips of extents 4x2x2, 2x1x1 and 1x1x1, retaining original
  one-mip coverage. New exact FP32-bit checks cover shrinking depth,
  signed/omitted XYZ offsets, components, negative/upper LOD, half-mip
  blends at 0.5/1.5 and sampler clamps at mip 1, with waves 4/8,
  five lanes, guards and repeat 2. Malformed shapes,
  non-heap handles, comparison samplers and dynamic/undefined/poison or
  partially undefined offsets remain rejected. Non-fragment implicit 3D
  sampling, non-32-bit 3D loading and other ordinary shapes are unchanged; no bound/static descriptor
  materialization or owned D3D12 execution is enabled.
  Heap-backed FP32 3D `Load` now executes through normal/reference/AOT paths
  with three-component i32 XYZ coordinates, an i32 mip, constant signed
  XYZ offsets and scalar/vector results up to the declared texel width
  (at most four). No sampler is required. Offsets become wrapping integer
  coordinate adds; the volume-load helper receives explicit mip and sample
  zero. Native DXIL's all-undefined omitted 3D offset raises to zero;
  partially undefined offsets stay rejected.
  All three per-mip extents are checked; negative/out-of-range
  coordinates and invalid mips read zero. Compiled-stage tests cover
  multi-mip/XYZ/component selection, offsets, bounds, partial waves,
  helpers, masks, guards and repeats. Native SDK DXC bindless shaders
  execute in all three paths with offsets, components, divergent
  coordinates, invalid mips, five lanes, guards and repeats on one mip;
  the original YAML fixture uses one mip. Malformed shapes, non-heap handles,
  non-32-bit texels and dynamic/undefined/poison offsets remain rejected.
  Other loading/sampling, SPIR-V lowering, artifact ABI 8 and argument
  layouts are unchanged. No bound/static descriptor materialization or
  owned D3D12 execution is added.
  Heap-backed signed/unsigned i32 3D `Load` now also executes through
  normal/reference/AOT paths with the same coordinate/mip/offset rules
  and scalar/vector widths one through four. Result scalar types must
  match the texel; exact integer bits survive without FP32 conversions.
  Invalid mips and lower/upper per-mip XYZ bounds read zero. Compiled
  normal/reference compute tests distinguish high-bit/negative values,
  mips, XYZ/components, offsets, bounds, partial waves, guards and repeats;
  fragment tests add integer outputs, divergent mips and helper masks.
  Native SDK DXC `int4`/`uint4` shaders execute in all three paths with
  signed offsets, exact components, divergent coordinates, invalid mips,
  five lanes, guards and repeats in the original one-mip fixture.
  Mixed scalar types, non-32-bit texels, too-wide results,
  non-heap handles and dynamic/undefined/poison offsets remain rejected,
  including dynamic-offset admission in all three paths. Existing FP32
  loads, sampling, other dimensions, SPIR-V lowering, artifact ABI 8 and
  argument layouts are unchanged; SPIR-V integer loads share the runtime
  bounds correction. No bound/static descriptors or owned D3D12 execution
  are added.
  Heap-backed FP32 1D/array `Load` now executes through normal/reference/
  AOT paths with scalar i32 X or paired `(X,layer)` coordinates, an i32
  mip, constant signed scalar spatial offsets and scalar/vector FP32
  results up to the declared texel width (one through four). Wrapping
  adds affect only X; layers stay separate. No sampler/derivatives are
  needed. Native omitted undefined/poison spatial offsets normalize to
  zero; canonical dynamic/undefined/poison offsets reject. Invalid mips,
  per-mip X bounds and independent layer bounds return zero. Compiled
  normal/reference compute tests distinguish three mips, layers, X and
  components with all result widths, offsets, bounds, partial waves,
  guards and repeats. Fragment tests add divergent mips/layers and helper
  masks. Native SDK DXC shaders execute in all three paths with signed/
  omitted offsets, invalid mips, five lanes, guards and repeats on one
  mip in the original fixture. Dynamic-offset admission rejects
  in all three paths. Non-32-bit 1D/array loads remain unsupported. Existing
  3D loads, sampling, SPIR-V lowering, artifact ABI 8 and argument layouts
  are unchanged; SPIR-V FP32 loads share the runtime bounds correction.
  No bound/static descriptors or owned D3D12 execution are added.
  Heap-backed signed/unsigned i32 1D/array `Load` now also executes through
  normal/reference/AOT paths with the same coordinate/mip/offset rules
  and widths one through four. Matching result/texel scalar types and
  integer helpers preserve exact bits without FP32 conversions. Invalid
  mips, per-mip X bounds and independent layer bounds read zero. Compiled
  normal/reference compute tests distinguish exact high-bit/negative and
  adjacent non-FP32-exact components, mips, layers and X with all result
  widths, offsets, bounds, partial waves, guards and repeats; fragment
  tests add integer outputs, divergent mips/layers and helper masks.
  Native SDK DXC plain/array `int4`/`uint4` shaders execute in all three
  paths with signed/omitted offsets, exact components, invalid mips, five
  lanes, guards and repeats in the original one-mip fixture.
  Dynamic-offset admission rejects in all three paths. Mixed scalar
  types, non-32-bit texels, too-wide results, non-heap handles and
  dynamic/undefined/poison offsets remain unsupported. Existing FP32 loads,
  3D loads, sampling, SPIR-V lowering, artifact ABI 8 and argument layouts
  are unchanged; SPIR-V integer users share the runtime bounds correction.
  No bound/static descriptors or owned D3D12 execution are added.
  Heap-backed FP32 2D/array `Load` now executes through normal/reference/
  AOT paths with paired `(X,Y)` or three-component `(X,Y,layer)` i32
  coordinates, an i32 mip, constant signed XY offsets and scalar/vector
  results up to the declared FP32 texel width (one through four).
  Wrapping adds offset only X/Y; layers stay separate. Existing helpers
  need no sampler/derivatives. Shared admission/lowering replaces the
  zero-offset-only fallback. Malformed coordinates, padded array offsets,
  integer/non-FP32 texels, too-wide results, non-heap handles and dynamic/
  undefined/poison or partial offsets reject. Native omitted undefined/
  poison XY offsets raise to zero; canonical offsets remain strict.
  Both per-mip spatial extents, invalid mips and independent layer bounds
  are checked; invalid accesses read zero. Compiled normal/reference
  compute tests distinguish rectangular mips, X/Y/layers/components, all
  result widths, offsets, bounds, partial waves, guards and repeats;
  fragment tests add divergent mips/layers and helper masks. Native SDK
  DXC plain/array shaders execute in all three paths with signed/omitted
  offsets, components, invalid mips, five lanes, guards and repeats on
  one, two and three real mips. The 5x3/2x1/1x1 chain retains two fixed
  layers and checks exact negative quarter-valued FP32 bits, signed/
  omitted offsets, first/last texels and all lower/upper X/Y/layer/mip
  bounds, with normal waves 4/8, five lanes, guards and repeat 2.
  Dynamic-offset admission rejects in all three paths.
  Existing 1D/3D loads, sampling, SPIR-V
  lowering, artifact ABI 8 and argument layouts are unchanged; SPIR-V
  FP32 users share the runtime bounds correction. No bound/static
  descriptors or owned D3D12 execution are added.
  Heap-backed signed/unsigned i32 2D/array `Load` now also executes through
  normal/reference/AOT paths with matching result/texel scalar types,
  widths one through four and the same coordinate/mip/offset rules.
  Integer helpers preserve exact bits without FP32 conversions. Invalid
  mips, per-mip X/Y bounds and independent layer bounds read zero.
  Compiled normal/reference compute tests distinguish exact high-bit/
  negative and adjacent non-FP32-exact components, rectangular mips,
  X/Y/layers, all result widths, offsets, bounds, partial waves, guards
  and repeats; fragment array tests add integer outputs, divergent
  mips/layers and helper masks. Native SDK DXC plain/array `int4`/`uint4`
  shaders execute in all three paths with signed/omitted offsets, exact
  components, invalid mips, five lanes, guards and repeats on one, two
  and three real mips. The 5x3/2x1/1x1 chain retains two fixed layers
  and checks first/last texels, all lower/upper X/Y/layer/mip bounds
  and normal waves 4/8. Dynamic-offset admission rejects
  in all three paths. Mixed scalar types, non-32-bit texels, too-wide
  results, non-heap handles and dynamic/undefined/poison or partial
  offsets reject. Existing FP32 loads, 1D/3D loads, sampling, SPIR-V
  lowering, artifact ABI 8 and argument layouts are unchanged; SPIR-V
  integer users share the runtime bounds correction. No bound/static
  descriptors or owned D3D12 execution are added.
  Heap-backed FP32 `Texture2DMS::Load`/`Texture2DMSArray::Load` now has
  stage-independent CPU lowering with shape-matched i32 coordinates,
  an i32 sample, constant signed XY offsets and scalar/vector FP32
  results up to the declared width (one through four). Native opcode 66
  raises to `llvm.dx.resource.load.ms`; existing 2D/array helpers receive
  mip zero, the live sample and separate layers. Invalid samples return
  zero without aliasing another texel/row/layer. Compiled normal/reference
  compute tests distinguish four samples, components, coordinates/layers,
  offsets, bounds, partial waves, guards and repeats; fragment array tests
  add divergent samples/layers and helper masks. SDK DXC FP32 plain/array
  shaders pass import/raising and normal/reference/AOT-object execution
  with runner MS YAML. Integer MS execution is covered below.
  MS sampling and dynamic/undefined/poison or partial
  offsets remain unsupported, including dynamic-offset rejection in
  normal/reference/AOT paths. Artifact ABI 8 and argument layouts are
  unchanged; no bound/static descriptors or owned D3D12 execution are added.
  Heap-backed signed/unsigned i32 MS `Load` now also has stage-independent
  CPU lowering with the same coordinate/sample/offset rules and widths
  one through four. Integer helpers preserve exact bits with mip zero,
  live samples and separate layers. Invalid samples return zero instead
  of aliasing another texel/row/layer. Runtime and compiled
  normal/reference compute tests distinguish exact components, samples,
  coordinates/layers, offsets and bounds, partial waves, guards and
  repeats; fragment array tests add integer outputs, divergent samples/
  layers and helper masks. SDK DXC `int4`/`uint4` shaders pass native
  import/raising and normal/reference/AOT-object execution with runner
  MS YAML. Dynamic integer offsets fail normal/reference/AOT admission.
  Mixed scalar types, non-32-bit texels, too-wide results, non-heap handles
  and undefined/poison or partial offsets remain rejected. Native raising,
  FP32 MS/single-sample loads, sampling, artifact ABI 8 and argument
  layouts are unchanged; no bound/static descriptors or owned D3D12
  execution are added.
  MS heap YAML accepts `dimension: 2d-ms`/`2d-ms-array` and a positive
  `sample-count`, default 1. Native SDK DXC FP32/SINT/UINT plain/array
  fixtures distinguish all four samples, rows/layers/components,
  signed/omitted offsets, negative/upper samples and layers, waves 4/8,
  seven lanes, guards and repeats through normal/reference/object paths.
  Canonical tests also cover counts 1/2/8, zero-filled unspecified
  samples and last-row/layer addressing. MS images retain one mip;
  this is not a D3D12 MSAA-quality/support or owned-device claim.
  Heap-backed FP32 3D implicit `Sample` now has fragment-stage CPU lowering
  with default samplers, three-component U/V/W coordinates, constant signed
  XYZ offsets and scalar/vector results up to the declared texel width (at
  most four). Six real coarse spatial derivatives drive LOD with zero bias
  and no shader MinLod clamp. Compiled fragment tests isolate signed U/V/W
  derivatives on both screen axes for mips 0/1/2, offsets, all four result
  widths, helpers, partial waves, guards and repeats. Native SDK DXC pixel
  shaders pass import/raising; real shader-clamped variants remain unraised.
  This compute runner still rejects implicit 3D sampling in normal,
  reference and AOT admission. Non-32-bit DXIL 3D loading, other sampling, SPIR-V,
  artifact ABI 8 and argument layouts remain unchanged. No bound/static
  descriptor materialization or owned D3D12 execution is added.
  Heap-backed FP32 1D/array implicit `Sample` now has fragment-stage CPU
  lowering with default samplers, scalar U or `(U,layer)`, constant signed
  scalar offsets and scalar/vector results up to the declared texel width
  (at most four). Real coarse U derivatives drive LOD with zero shader
  bias and an undifferentiated array layer. Compiled fragment tests verify
  signed derivatives on both screen axes, mips 0/1/2, layers, offsets,
  components, helpers, partial waves, guards and repeats. Native SDK DXC
  plain/array pixel shaders pass import/raising; real shader-clamped variants
  remain unraised. This compute runner still rejects implicit 1D sampling
  in normal/reference/AOT admission, even with valid constant offsets.
  Non-fragment calls and malformed/dynamic/undefined/poison offsets remain
  unsupported. Other ordinary shapes are unchanged;
  no bound/static descriptor materialization or owned-device execution is added.
  FP32 `SampleGrad` now has checked opcode-63 import/raising for 1D/array,
  2D/array, 3D and cube/array textures, with scalar/vector FP32 texels up to
  four components and default samplers. Coordinates/layers, spatial gradients,
  signed/dynamic spatial offsets and optional real MinLod clamps survive;
  gradient width excludes array layers. Undefined/poison clamps select the
  plain intrinsic; zero/dynamic clamps remain clamped. Seven-shape roundtrips,
  raw guards, idempotence and native DXC plain/clamped import pass.
  Heap-backed FP32 1D/2D/3D/cube gradients (including arrays) execute in any stage
  through normal/reference/AOT paths, using default samplers, shape-matched
  coordinates/gradients and optional shader clamps. 1D/2D/3D use constant signed
  offsets; cubes require unused zero/undef/poison scalar offsets.
  Scalar/vector results cannot exceed the declared texel width (at most
  four). Supplied gradients drive existing LOD/projection helpers without
  derivative substitution or an LOD-zero approximation. Array elements
  remain coordinate-only, excluded from scalar U, two-component U/V or three-component
  raw-direction gradients. Compiled-stage tests verify multi-mip/face/
  element selection; runner image YAML also prepares mip chains. Its
  cube/array fixture tests divergent MinLod clamps and linear half-mip
  blending, in addition to faces/elements, offsets, components, partial
  waves and repeats. 1D gradients/offsets are scalar; 3D coordinates,
  gradients and offsets have three U/V/W components. Actual SDK DXC SM6.6
  2D/plain-array and 3D gradient shaders execute three real rectangular
  mips through normal waves 4/8, reference and ARM64 AOT-object paths.
  Live U/dX, negative V/dY and 3D W/dX distinguish mips 0/1/2. Shader
  MinLod floors 0.5/1.5 check exact FP32-bit half-mip blends; independent
  plain-gradient samples check sampler MinLod=MaxLod=1, with signed/
  omitted offsets, fixed layers, shrinking depth, components, five
  lanes, guards and repeat 2. Native 1D/plain-array and cube/plain-array
  gradient shaders also execute three real mips through the same CPU
  paths. Scalar 1D gradients/offsets, negative U/dY, scalar cube texels/
  results, all faces/elements and face-local tangent projection on both
  screen axes are checked. Joint cube direction/gradient scaling by
  one/two preserves LOD; exact-bit shader half-mip blends and sampler
  clamps at mip 1 pass with five/thirteen lanes, guards and repeat 2.
  Native gradient execution evidence now spans all seven raised shapes.
  All seven raised
  shapes have bounded CPU execution; dynamic or partially undefined 3D
  offsets remain rejected in normal/reference/AOT admission. No descriptor
  materialization or owned-device execution is enabled.
  This is binding preflight, not descriptor/table
  preparation or full shader/root compatibility.
  Descriptor-backed CBVs and cbuffer arrays are not promoted. This works in
  normal/reference compilation; pass it to `feme` when producing an AOT
  object, not to `feme-run --object`. It cannot be combined with inspection.
  The default inferred behavior is unchanged.
- `--heap=<file>`: a YAML file describing the resource heap a dispatch runs
  against. Buffer `bindings` entries can specify `register-class: srv` or
  `register-class: uav`. Omitting it (or using `unspecified`) retains legacy
  matching only when that space/register has one reflected namespace.
  Ambiguous unqualified bindings, invalid classes, duplicate qualified
  bindings and unmatched qualified bindings are errors. Qualified output
  labels include the class, e.g. `binding[srv:7:3][0]`; unqualified labels
  are unchanged. This applies in both inferred and explicit-root modes.
  AOT objects use artifact ABI 8; version-7 objects must be recompiled.

  ```yaml
  root-constants: [1, 2, 3, 4]   # optional: uint32 words
  resource-heap:                 # the shader's *logical* dynamic heap
    - index: 0
      size: 64                  # bytes to allocate (zero-filled)
      data: [0, 1, 2, 3]        # optional: little-endian uint32 words
  bindings:                      # the shader's traditionally-bound resources
    - space: 0
      register: 0
      register-class: uav         # optional: srv, uav, or unspecified
      entries:
        - index: 0              # array element within the binding's range
          size: 64
          data: [0, 1, 2, 3]
  images:                        # the shader's *logical* dynamic image heap
    - index: 0
      dimension: 1d              # also 1d-array/2d/2d-array/2d-ms/2d-ms-array/cube/cube-array/3d
      extent: [4, 1]
      format: r32g32b32a32_float
      sample-count: 1            # positive; must be 1 for non-MS dimensions
      mip-levels: 1              # [1, 32]; must be 1 for MS dimensions
      data: [0, 1, 2, 3]         # uint32 words, mip/layer/row/texel/sample-major
  samplers:                      # the shader's *logical* dynamic sampler heap
    - index: 0
      min-filter: nearest        # nearest/linear
      address-u: clamp-to-edge   # clamp-to-edge/repeat/mirror/clamp-to-border
  image-bindings:                 # traditionally-bound images (register(tN, spaceM))
    - space: 0
      register: 0
      entries:
        - index: 0               # array element within the binding's range
          dimension: 1d
          extent: [4, 1]
          format: r32g32b32a32_float
          data: [0, 1, 2, 3]
  sampler-bindings:                # traditionally-bound samplers (register(sN, spaceM))
    - space: 0
      register: 0
      entries:
        - index: 0
          min-filter: nearest
          address-u: clamp-to-edge
  ```

  Cube images use nonzero square face extents. `dimension: cube` prepares
  six physical face layers automatically. `dimension: cube-array` requires
  an explicit positive multiple of six in `array-layers` (12 means two
  cubes), retaining the descriptor ABI's physical-layer convention.
  Data is face/layer-major in +X, -X, +Y, -Y, +Z, -Z order for each cube.
  Incomplete face counts, overflowing subresource sizes and sizes exceeding
  the host allocation limit are errors.

  `2d-ms`/`2d-ms-array` accept a positive `sample-count` (default 1).
  MS extents must have nonzero X/Y and no depth component; MS arrays
  require positive `array-layers`. Non-MS dimensions require count 1.
  Each texel's samples are contiguous, in increasing sample-index order;
  format components are contiguous inside each sample. Rows and physical
  array layers follow outside that sample-interleaved texel layout.
  Checked row/slice/layer sizing includes sample count and rejects overflow
  and host allocation limits before allocation. Unspecified data remains
  zero-filled. The schema applies equally to `images` and `image-bindings`.
  Sample counts describe CPU storage only, not D3D12 MSAA-quality/support.
  MS images require one mip.

  Non-MS images accept `mip-levels` in [1, 32], default 1. Chains require
  nonzero extents; each spatial extent and 3D depth halves per level,
  clamped to one. Array/cube physical layers remain fixed. Mips are
  contiguous in increasing level order, each containing all physical
  layers/depth slices before the next mip. Rows, texels and format
  components follow inside each layer/slice. For example, a `5x3`
  `r32_uint` image with three mips contains 15, 2 and 1 words; a two-layer
  array contains 30, 4 and 2 words. Cumulative offsets and subresource
  products are checked before allocation. Missing data is zero-filled;
  excess word data is truncated as before. The schema applies equally to
  `images` and `image-bindings`. Counts describe CPU storage, including
  repeated one-texel levels, not D3D12 legal mip-count/format support.
  Native SDK DXC FP32/SINT/UINT 2D/plain-array loads execute three-mip
  fixtures in normal/reference/AOT-object modes with extents 5x3/2x1/
  1x1 and two fixed layers, retaining one/two-mip regressions. Exact
  negative/fractional FP32 and integer bits check signed/omitted offsets,
  first/last texels/components and all lower/upper X/Y/layer/mip bounds,
  with normal waves 4/8, five lanes, guards and repeat 2.
  Canonical fixtures add non-power-of-two
  1D/2D arrays, 3D depth, mip-31 bounds, zero-fill and cube mip blending.
  Native SDK DXC FP32/SINT/UINT 1D/plain-array and 3D load fixtures also
  execute three real mips through normal/reference/AOT-object paths.
  Widths 5/2/1 keep fixed array layers; rectangular 3D extents
  4x2x2/2x1x1/1x1x1 shrink depth. Signed offsets select first/last
  texels and components. Invalid X/layers/mips and upper XYZ bounds
  read zero, with exact integer bits, native raising, waves 4/8, five
  lanes, guards and repeat 2 checked. Original one-mip coverage remains.
  This evidence required no compiler/runtime implementation change.
  Image-binding preparation does not imply bound-image or owned D3D12
  execution; MS sampling and owned-device gaps remain open.

  Every `resource-heap`/`bindings` entry becomes an unstructured,
  host-writable raw buffer (`ResourceKind::Raw`, `FEME_DESCRIPTOR_UAV`
  set). A `resource-heap` entry is the buffer a shader's native
  `ResourceDescriptorHeap[index]` indexes. A `bindings` entry (roadmap
  milestone 11) is matched by (`space`, `register`) to a traditional
  binding (e.g. `register(u0, space0)`)
  `feme::cpu::BoundResourceNormalizationPass` normalized into the CPU
  target's reserved heap prefix (see "Bound-resource normalization" in
  [../FeMeCPUDesign.md](../FeMeCPUDesign.md)); an unbound slot within a
  declared range is left as the zero descriptor.

  `root-constants` are packed into the CPU ABI's byte block in normal,
  reference and object modes, with storage retained across repeated
  dispatches. The shader's single inferred cbuffer binding accepts
  homogeneous 16-/32-/64-bit integer and floating-point rows; the YAML
  still uses `uint32` words, not typed scalar values. Each scalar must
  fit both its declared array element and the supplied block, otherwise
  it reads zero. Normal/reference dispatch reject an undersized block
  against reflection; object mode has no reflected required span and
  exercises the compiled guards directly for short or empty input.

  `images`/`samplers` (roadmap steps R31/R41) are the image-heap/
  sampler-heap counterparts of `resource-heap`: each entry is the
  descriptor a shader's native `ResourceDescriptorHeap[index]`/
  `SamplerDescriptorHeap[index]` indexes. `image-bindings`/
  `sampler-bindings` (roadmap item `L320`) are the corresponding
  counterparts of `bindings`: each entry is matched by (`space`,
  `register`) to a traditionally-bound image/sampler range (e.g.
  `register(t0, space1)`/`register(s0, space2)`)
  `feme::cpu::SPIRVResourceLoweringPass` normalized into the CPU target's
  reserved image/sampler heap prefix; before `L320`, there was no way to
  describe such a binding at all, so every traditionally-bound image or
  sampler silently resolved to the zero-initialized reserved prefix
  instead of any host-supplied descriptor. An unbound slot within a
  declared range is left as the zero descriptor, same as `bindings`.

## OUTPUT

After the dispatch completes, `feme-run` prints each heap entry's final
contents as `uint32` words, one line per entry, for `FileCheck` to match:

```
heap[0]: 0 1 2 3
binding[0:0][0]: 4 5 6 7
image[0]: 0 1 2 3
sampler[0]: ...
image-binding[0:0][0]: 0 1 2 3
sampler-binding[0:0][0]: ...
```

## EXAMPLES

```shell
# Run a shader that writes its own dispatch thread id into a raw buffer,
# and check the buffer's contents afterwards.
feme-run --wave-size=4 --groups=1,1,1 --heap=heap.yaml shader.ll | FileCheck shader.ll

# Compile real HLSL to a DXIL DXContainer with Clang, and run it the same
# way (see feme/test/Tools/feme-run/HLSL for full examples). A
# `register(u0)`-bound resource is normalized into the bindless heap
# automatically; the heap YAML supplies its descriptor as a `bindings`
# entry.
clang -target dxil--shadermodel6.5-compute -c shader.hlsl -o shader.dxcontainer
feme-run --wave-size=4 --groups=1,1,1 \
    --heap=heap.yaml shader.dxcontainer | FileCheck shader.hlsl
```

## EXIT STATUS

`feme-run` returns 0 on success, and a non-zero value if parsing the input,
compiling the shader, or reading the heap file fails.
