# `feme` — FeMe: FrontEnd for the MiddleEnd

## SYNOPSIS

```shell
feme [options] <input file>
```

## DESCRIPTION

`feme` is FeMe's primary, user-facing command line tool: given an input
shader/IR file and a destination format or target ISA, `feme` detects the
input's format, imports it, translates it into FeMe's internal
representation, and retargets/exports it to the requested output — the same
way Clang's driver builds compile+assemble+link jobs from `-o`/`--target`
rather than requiring callers to invoke each compilation stage by hand. See
the "Command Line Tool(s)" section of [../Design.md](../Design.md) for the
full design.

`feme` drives `feme::Driver` (see the "Driver" section of
[../Design.md](../Design.md)). `feme` detects its input format from the
input file's contents; currently detected formats are `dxil`, `spirv`, and
`dxbc` (a legacy DXBC `DXContainer`; any input whose format cannot be
detected this way is rejected with a diagnostic). `--target` may name
`dxil`, `spirv` (re-serializing back to that format via its own LLVM
backend), or any other LLVM target triple registered with the
`TargetRegistry` (e.g. `amdgcn-amd-amdhsa`) for real-ISA retargeting.

Current limitations. Retargeting a DXIL *or* DXBC input to any target first
requires undoing its `dx.op.*` calling convention and recovering the shader
model and entry point information from its `dx.*` metadata (a DXBC input
reaches this same `dx.op.*`-calling-convention shape via `feme::dxsa::
translateToLLVMIR`, the DXBC → DXIL edge of FeMe's Translation Matrix, so
it needs the identical raising); retargeting to SPIR-V or `amdgcn-*`
additionally requires re-expressing the result in that target's own
intrinsics and resource conventions. All of these passes are deliberately
incremental (see the DXIL, "Raised LLVM IR -> AMDGPU", and "Raised LLVM IR
-> SPIR-V" sections of [../Design.md](../Design.md)): typed buffers
(`Buffer`/`RWBuffer`), textures (`Texture*`/`RWTexture*` load/store, both
the modern bindless and legacy `!dx.resources` binding paths), and cbuffer
scalar loads (any of `CBufferLoadLegacy`'s 32-/16-/64-bit row widths) are
covered, including default/comparison sampler handle modes. Raw and
structured buffer accesses when retargeting
to `amdgcn-*` (typed buffer/texture/cbuffer accesses alone are covered
there; raw/structured buffers are not yet), texture sampling
(`Sample`/FP32 `SampleBias`/`SampleLevel` and scalar FP32 comparison raise but
have no `amdgcn-*`/NVPTX lowering yet),
and shader input/output signature ops are not, so a shader using those will
fail at this stage rather than at any point specific to `feme`'s own
Driver/CLI logic -- a DXBC-derived module is limited to the same set, on
top of `feme::dxsa::translateToLLVMIR`'s own known gaps (see the DXBC
section of [../Design.md](../Design.md)): notably, a DXBC compute shader's
`dcl_thread_group` dimensions do not yet reach DXIL's `NumThreads`
metadata, and no DXBC graphics-stage (vertex/pixel/...) shader's signature
I/O is retargetable to DXIL/AMDGPU yet either, DXBC- or DXIL-derived, since
neither `feme::dxil::OpRaisingPass` nor LLVM's DirectX target itself raises
`dx.op.loadInput`/`storeOutput` (no idiomatic intrinsic models "read a
whole signature element" the way resource ops do).

For CPU targets, native and legacy DXIL raw/structured stores also support
contiguous masks 3/7/15 for packed two/three/four-component i32/FP32
values. Scalar behavior remains; sparse/dynamic/oversized masks, packed
non-32-bit types and mixed active fields remain unraised. Native SDK DXC
SM6.6 heap and SM6.0 bound compute tests execute through normal waves
4/8, reference and ARM64 AOT-object paths with exact bits, field strides/
sub-offsets, divergent masks, five lanes, guards and two repeats.
Existing vector helpers reject an entire partly out-of-bounds write and
write exactly 12 bytes for three components. Legacy AOT takes an explicit
physical heap.

Packed i32/FP32 raw/structured loads also use existing CPU vector helpers.
Modern opcode 139 accepts contiguous masks 1/3/7/15 and keeps explicit
widths for partial result uses; legacy opcode 68 infers width only from
complete leading-component uses. Scalar type support is preserved.
Invalid masks/types, sparse/isolated-later legacy uses, status/aggregate
uses and unused loads stay unraised. Native SM6.6/SM6.0 tests verify
exact-bit `Load2`/`Load3`/`Load4` and structured uint4/float3 fields,
divergent uses, guards, last-valid vectors, all-zero partial/large-address
reads and two repeats across the same CPU modes. A separate tail
descriptor avoids legacy source-width coalescing; general legacy partial/
coalesced-width semantics remain unproven. No new runtime/SPIR-V code,
status support, GPU-target lowering, ABI or owned D3D12 behavior is implied.

Native FP32 structured tests also cover float2/float3/float4 loads and
stores at offset 4 with strides 12/16/20 across the same SM6.6/SM6.0 CPU
modes. Exact copies preserve negative zero, signed subnormals/infinities/
quiet-NaN payloads and adjacent values near one without arithmetic.
Truncated final vectors and invalid indices read all zero; partial/
invalid writes preserve inputs. Five lanes, divergent copies, every word
of 732 output bytes, record/trailing guards and two repeats pass, including
12-byte float3 stores. No wider legacy admission, implementation or
general FP32 arithmetic/conformance result is implied.

Native raw/structured i32 resource-atomic tests cover add/and/or/xor,
signed/unsigned min/max, exchange and successful/failed compare-exchange
in SM6.6 heap and SM6.0 bound shaders across the same CPU modes. Existing
getpointer/atomicrmw/cmpxchg and CPU helpers preserve stride 12 and field
offsets 4/8. Independently initialized lanes check exact old values,
high bits, UINT_MAX + 1 wraparound, divergent increments, invalid-address
zero/no-op behavior and guards twice. Legacy AOT uses a physical heap.
No contention/memory-order/barrier conformance, typed/image/int64 breadth,
new implementation, descriptor materialization or ABI change is implied.

Native R32_SINT/R32_UINT typed atomics also check all nine binary operations
across the two formats, successful/failed compare-exchange, exact old
values, wraparound, divergent increments and element-index bounds across
the same CPU modes. Modern scalar and legacy atomic-only four-component
fallback handles retain signedness. Atomic exchange initializes lane-owned
elements; guarded 28-byte state/464-byte output words pass twice.
No legacy width inference, contention/order/barrier, other-format/image/
int64 atomic or ABI change is implied.

Full-mask typed stores now preserve opcode 69 mask-15 i32/FP32 payloads
through narrower handles as four-channel canonical stores. Handle width/
signedness stay intact; existing vector helpers honor the bound format
without scalar helpers' 0/0/1 fills in other channels. Native SM6.6/SM6.0
scalar SINT/UINT/FP32 stores pass exact-output R32/RGBA32 cases across
the same CPU modes, checking all broadcast channels, special bits,
divergent stores, invalid indices, texel guards and two repeats. Invalid/
nonmatching partial masks, mixed types and non-32-bit promotion remain
unraised. This closes the scalar-store gap, not general typed-load/store,
owned-device, runtime/SPIR-V, loads/status, atomic or ABI behavior.

Native four-component typed loads additionally cover SDK DXC SM6.6/SM6.0
SINT/UINT/FP32 SRV/UAV shaders on nine R32/RGB32/RGBA32 formats across
the same CPU modes. Exact bits, valid R32 zero-G/B/one-alpha defaults,
RGB32's real 12-byte stride and alpha one versus all-zero incomplete/
invalid loads, valid-end accesses, divergent use, complete input/
output words, guards and two repeats pass. Distinct inputs prove namespace
separation; legacy AOT uses explicit space/register/namespace-ordered heaps.
This is bounded CPU evidence, not new admission, status, runtime/SPIR-V,
ABI or owned-device behavior.

Native scalar/vector-two typed loads now cover SDK DXC SM6.6/SM6.0
SINT/UINT/FP32 SRV/UAV shaders on matched R32/RG32 formats across the
same CPU modes, now also on wider RGB32 descriptors at both widths.
Narrow handles/helpers preserve exact bits and RGB32's physical 12-byte
stride. Incomplete RG32/RGB32 elements return zero; RGB32 missing Z is
invalid even when R/G fit. Upper/negative indices return zero at both
widths. Namespace separation, valid-end accesses, divergence,
all input/output words, guards and two repeats pass. Legacy AOT uses
explicit space/register/namespace-ordered heaps. This is narrow CPU
evidence, not new admission, status, runtime/SPIR-V, ABI or general typed/
owned-device closure.

Typed-buffer `GetDimensions` now raises opcode-72 field-zero queries to
`llvm.dx.resource.getdimensions.x` and lowers to the existing CPU typed
element-count helper. Complete physical texels are counted from descriptor
byte size/format, not shader width. Only unused/zero mip operands and
field-zero extracts are supported; mixed fields, aggregate uses and meaningful
mips remain unraised. Native SDK DXC
SM6.6/SM6.0 SINT/UINT/FP32 SRV/UAV queries pass RGB32 and mixed
R32/RG32/RGBA32 layouts across the same CPU modes, including partial/
too-short views, namespaces, divergence, guards and repeats. Runtime/
SPIR-V code, ABI 8 and argument layouts remain unchanged; no stores,
advertisements or owned-device closure are added.

`ByteAddressBuffer`/`RWByteAddressBuffer.GetDimensions` now also raises
field-zero queries and lowers to the existing raw metadata helper with
stride one/prefix zero. The result is bytes, not DWORDs or structured
elements. Native SDK DXC SM6.6/SM6.0 SRV/UAV queries pass across the
same CPU modes, with namespace separation, divergence, output query/store
coexistence, guards and repeats. Runtime tests cover odd-byte counts,
metadata-only reads and inactive/unbound/bounds zeros. Buffer xy fields
and meaningful mips remain unsupported; runtime/
SPIR-V code, ABI 8 and argument layouts are unchanged.

`StructuredBuffer`/`RWStructuredBuffer.GetDimensions` now also executes
through field-zero raising and the raw metadata helper, using the handle's
declared structure size as stride, prefix zero. Counts exclude incomplete
elements. DXC supplies the HLSL stride result as a constant; it is preserved,
not interpreted as DXIL field 1. Native SM6.6/SM6.0 SRV/UAV cases pass
12/20-byte strides, partial/too-short views, namespace separation,
divergence, guards and repeats across the same CPU modes. Buffer xy fields,
aggregate uses and meaningful mips remain unsupported. Runtime/SPIR-V
code, ABI 8 and argument layouts are unchanged; no owned-device or
general conformance closure is established.

Heap-backed `Texture2D`/`RWTexture2D.GetDimensions` now lowers scalar/
pair FP32/i32 queries to the existing CPU width/height metadata helper
in any stage, with no sampler or derivatives. Width-only, height-only and
paired outputs pass SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
including a three-mip base extent, divergent/unbound indices, query/load
coexistence, all words, guards and repeats. Admission and lowering share
the same predicate. Sampled plain 2D explicit mips/counts use the separate
path below. 2D-array/MS/3D/cube shapes, non-32-bit declarations,
depth/aggregate uses and bound/static images remain unsupported by
this path. Runtime/SPIR-V code, buffer dimensions, ABI 8 and argument
layouts remain unchanged; no owned-device closure is established.

Heap-backed `Texture1D`/`RWTexture1D` and their array counterparts now
also execute mip-zero FP32/i32 dimensions through the existing metadata
helpers. Plain 1D accepts scalar width only; array 1D accepts scalar width
or width/layer count. Native SDK DXC SM6.6 passes waves 4/8, reference
and ARM64 AOT, including three-mip base widths, distinct layer counts,
divergent/unbound indices, exact integer query/load results, all words,
guards and repeats. Plain 1D pair and plain 1D explicit-mip queries remain
rejected in normal/reference/AOT paths. No sampler/derivatives are
needed. Runtime/SPIR-V code, ABI 8, layouts and advertisements
remain unchanged; no bound/static images or owned-device closure.

Heap-backed sampled `Texture2D.GetDimensions(mip, width, height, levels)`
now preserves an explicit i32 mip and total mip count through the existing
`getdimensions.levels.xy` intrinsic. LLVM forward lowering and DXIL
raising map DXIL count field 3 to vector element 2; CPU lowering reuses
shared size/level metadata helpers without sampler/derivatives, in any
stage. Native SDK DXC SM6.6 passes waves 4/8, reference and ARM64 AOT
for three valid mip extents, fixed counts, divergent/unbound indices,
query-derived exact integer loads, all words, guards and repeats.
Queries whose count output is unused now also execute. Writable/unsupported-
shape/undefined-mip canonical forms reject; depth/aggregate uses remain
unraised. Shared helper clamping is unchanged; native out-of-range-mip
semantics are not claimed. Runtime/SPIR-V code, ABI 8 and layouts remain
unchanged; no bound/static images or owned-device closure is added.

Heap-backed sampled `Texture1DArray.GetDimensions(mip, width, layers, levels)`
now uses the same canonical form and shared CPU eligibility predicate,
selecting the existing 1D-array size-at-LOD helper and total-level helper.
Native SDK DXC SM6.6 passes waves 4/8, reference and ARM64 AOT for
widths 7/3/1, two layers/three mips and widths 5/2, three layers/two
mips. Query-derived exact SINT loads, divergent/unbound indices, skipped
stores, every word, guards and repeats pass. Count-unused queries also
execute. Writable arrays and plain 1D explicit queries remain unsupported.
Only valid mips are measured; shared helper clamping, runtime/SPIR-V code,
ABI 8 and layouts are unchanged. No owned-device closure is established.

Three-component typed i32/FP32 loads now lower through existing vector-four
helpers followed by XYZ selection; physical descriptor stride/bounds are
unchanged. Portable i32/FP32 regressions and native SM6.6/SM6.0 FP32
SRV/UAV RGB32/RGBA32 cases check 12-byte stride, W discard, incomplete/
invalid all-zero reads, exact bits, namespace separation, valid-end loads,
divergence, guards and two repeats across the same CPU modes. This
replaces nonexistent typed `v3` calls without new runtime/SPIR-V, store,
status, ABI or owned-device behavior; general format closure is not claimed.

Native integer-three typed loads now execute SDK DXC SM6.6/SM6.0 `int3`/
`uint3` SRV/UAV shaders on RGB32/RGBA32 SINT/UINT across the same CPU
modes. Shared RGB32 integer reads now use a real 12-byte stride, preserve
exact bits and supply alpha one. Typed scalar/vector-two/vector-four reads,
complete-element dimensions, integer image loads and fixed-alpha borders
have runtime tests. Native XYZ order, distinct-W discard, incomplete/
invalid all-zero reads, namespace separation, divergence, guards and
repeats pass. RGB32 integer typed stores remain tested no-ops; no Vulkan
advertisements, raising/lowering, SPIR-V code, ABI 8 or argument layouts
change. General typed-format and owned-device closure remain open.

A SPIR-V input is currently limited to shaders that use neither resources
nor builtin variables, since MLIR's `SPIRVToLLVM` conversion -- which
`feme` reuses rather than reimplementing -- has no patterns for SPIR-V image
types. See "Known gap: `spirv` dialect -> `llvm` dialect conversion
coverage" in [../Design.md](../Design.md).

## OPTIONS

* `--target=<format>`

  Output format/target to translate to (e.g. `dxil`, `spirv`, or a target
  triple).

* `-o <file>`

  Write output to `<file>`. If omitted, output is written to standard
  output.

* `--root-signature-constants`

  For CPU targets, select the checked root signature's sole visible inline
  constant parameter instead of inferring a cbuffer binding. The parameter's
  DWORD count determines the required root span and clips reads. Its full
  span and register/space remain reflected even when no cbuffer reads
  survive. Missing or
  invalid layouts, multiple visible parameters, denied stages and local
  signatures are errors. Before normalization, live bound-resource
  declarations in the entry and directly called helpers must match class,
  space and stage visibility, with one root binding containing each full
  declaration. Arrays cannot span adjacent ranges or individual static
  samplers; dynamic declarations and indirect calls fail validation.
  Root SRV/UAV descriptors require raw or structured buffers; typed buffers
  and textures require descriptor tables. Live original native heap
  handles require `CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED` for resources or
  `SAMPLER_HEAP_DIRECTLY_INDEXED` for samplers. This precedes normalization;
  generated heap accesses emulating traditional bindings do not require
  these flags. Visible static samplers must match the declared default/
  comparison mode; minimum/maximum filters remain non-comparison.
  Descriptor-table/native-heap sampler contents, counter compatibility
  and descriptor promises remain outside this check.
  Legacy DXC sampler handles now retain default/comparison modes, so real
  native shaders reach static-mode checking. Valid and inferred sampling
  remains unsupported at bound-descriptor/comparison-access admission,
  not static sampler materialization or execution.
  Scalar FP32 `SampleCmpLevelZero` now raises to the canonical intrinsic
  after checked opcode-65 mapping. Valid bound access reaches the
  descriptor error. Canonical heap-backed scalar FP32 1D/2D/cube comparison,
  including array forms, now executes on the CPU using shared helpers.
  Plain 1D uses scalar FP32 U and a constant scalar i32 offset; 1D arrays
  use FP32 (U, layer) coordinates with the same scalar offset. Plain/array
  2D uses two/three FP32 coordinates and two constant i32 spatial offsets.
  Other dimensions and dynamic/undefined offsets fail explicit admission.
  An array's final coordinate selects the layer; offsets are spatial only.
  Cubes use FP32 direction vectors of width three (four for cube arrays).
  Their unused scalar i32 offset must be poison/undef or zero; nonzero/
  dynamic offsets fail admission rather than being silently discarded.
  Scalar FP32 implicit `SampleCmp` also has checked opcode-64 mapping,
  preserving real MinLod clamps (including zero and dynamic values) in
  `llvm.dx.resource.samplecmp.clamp`; undefined/poison clamps select
  `llvm.dx.resource.samplecmp`. Fragment-stage heap-backed scalar FP32
  1D/2D/cube forms, including arrays, lower through shared comparison helpers with
  coarse spatial/direction derivatives, implicit LOD and the optional shader MinLod clamp.
  Supported offsets and sampler reflection are retained. Compute/
  unannotated stages, other shapes and dynamic/undefined spatial offsets
  still fail explicit admission. Compiled fragment execution is tested;
  this does not enable implicit comparison in compute/reference dispatch,
  descriptor materialization or owned D3D12 rendering. Half/vector results
  remain unsupported.
  2D-array coordinates are FP32 (U, V, layer); only U/V are differentiated
  and the two offsets remain spatial. Plain 1D uses scalar FP32 U and a
  scalar constant i32 offset; 1D arrays use (U, layer) with the same offset.
  Only U is differentiated. Missing-layer coordinates fail
  admission. Compiled array tests check divergent layers/references,
  dynamic clamps and observable mip selection.
  Cubes differentiate their three raw direction components, not the fourth
  cube-array element coordinate. Unused scalar offsets accept poison/undef
  or zero only. Compiled cube/array tests check all six faces, both elements,
  both derivative axes, clamps, helpers and guarded partial waves; native
  DXC plain/clamped import is tested separately from bound execution.
  FP32 `SampleBias` also has checked opcode-61 import/raising, preserving bias,
  spatial offsets and optional real MinLod clamps for 1D/array, 2D/array, 3D
  and cube/array textures. Undefined/poison clamps select the plain intrinsic;
  real zero/nonzero/dynamic clamps select `llvm.dx.resource.samplebias.clamp`.
  Scalar and two-/three-/four-component FP32 texels use default samplers.
  Seven-shape LLVM roundtrips and native DXC pixel-shader import are tested.
  Heap-backed FP32 1D/2D/3D/cube biased sampling, including arrays, now executes
  in compiled fragment stages with real spatial/direction derivatives,
  implicit LOD, bias, supported offsets
  and optional shader MinLod clamps. Scalar/vector results up to the declared
  texel width (at most four) narrow the shared four-component result.
  Mip/offset/component selection, divergent bias/clamps, helpers and guarded
  partial waves are tested. Compute/unannotated stages, other shapes,
  non-heap handles, comparison samplers and dynamic/undefined spatial offsets
  reject explicitly; normal/reference/AOT compute rejection is tested.
  Descriptor materialization and owned D3D12 rendering remain unsupported.
  Biased 2D arrays use FP32 (U, V, layer), with only U/V differentiated and
  two spatial offsets. Compiled array tests check divergent layers/bias/
  clamps and observable layer/mip/offset/component selection; missing-layer
  coordinates still fail admission.
  Cubes use six raw-XYZ derivatives for face-projected implicit LOD; a
  separate cube-array element is never differentiated. Unused scalar offsets
  must be poison/undef or zero. Compiled tests check all faces, both elements,
  biased/clamped mip selection and components with guarded partial waves.
  Nonzero/dynamic cube offsets remain unsupported.
  Biased 1D sampling uses scalar U and a constant signed scalar i32 offset;
  arrays use (U, layer), differentiating only U and keeping offsets spatial.
  Compiled tests check mip-0/mip-1 footprints, both derivative axes,
  layer/mip/offset/component selection, divergent layers/bias/clamps and
  guarded partial waves. Malformed coordinates and dynamic/poison/undefined
  1D offsets reject. Implicit DXIL 1D sampling outside fragment stages and
  non-32-bit 1D/array loading remain unsupported.
  Biased 3D sampling uses FP32 (U, V, W), six coarse spatial derivatives and
  three constant signed i32 offsets. Compiled tests isolate U/V/W LOD on
  both screen axes, signed XYZ offsets, biased/clamped mips and components
  with guarded partial waves. Malformed coordinates/offset widths and
  dynamic or partially undefined offsets reject. Implicit DXIL 3D sampling
  outside fragment stages and non-32-bit 3D loading remain unsupported; seven-shape
  biased execution is not broader sampling conformance.
  Heap-backed FP32 cube/array `SampleLevel` now compiles through shared
  checked lowering in any stage, including native AOT. Default samplers,
  FP32 direction/array-element coordinates, FP32 LOD and result widths
  one through four up to the declared texel width are supported. Cubes
  retain an unused scalar zero/undef/poison offset. Helpers receive live
  explicit LOD, six zero derivatives, zero bias, separate elements and
  no shader MinLod clamp. All `SampleLevel` calls now avoid the legacy
  implicit-only fallback; malformed calls reject consistently.
  Native DXC cube/plain-array shaders execute two mips, exact FP32-bit
  half-mip blends and image/sampler clamps through normal waves 4/8,
  reference and AOT-object paths with guards and repeats. Compiled
  fragment tests add all widths, six faces, two cubes, three mips,
  helpers and masks. Other sampling, runtime/SPIR-V lowering, ABI 8
  and argument layouts are unchanged; owned-device gaps remain open.
  Heap-backed FP32 2D/array `SampleLevel` now uses shared checked lowering
  in any stage with default samplers, FP32 U/V or U/V/layer coordinates,
  FP32 LOD, constant signed XY offsets and results up to the declared
  texel width (one through four). Helpers receive live explicit LOD,
  zero derivatives/bias, separate layers and no shader MinLod clamp.
  Malformed shapes/offsets, non-heap handles, comparison samplers,
  non-FP32 texels and too-wide results reject without the old zero-offset
  fallback bypass. Native DXC plain/array shaders execute three mips,
  half-mip blends, signed/omitted offsets and sampler clamps through
  normal/reference/AOT-object paths, with waves 4/8, partial waves,
  guards and repeats. Compiled fragment tests add all result widths,
  mips/layers/coordinates and helper masks. Implicit 2D/cube sampling,
  runtime/SPIR-V lowering, artifact ABI 8 and owned-device gaps are unchanged.
  Heap-backed FP32 1D/array `SampleLevel` now compiles in any stage, including
  native AOT, with default samplers, scalar U or `(U,layer)` coordinates,
  constant signed scalar offsets and scalar/vector results up to the
  declared texel width (at most four). Explicit LOD is preserved with zero
  derivatives/bias and unchanged sampler LOD behavior. Malformed shapes,
  non-heap handles, comparison samplers and dynamic/undefined/poison offsets
  remain unsupported. Native SDK DXC bindless 1D/array shaders execute
  through normal/reference/AOT paths with three real mips of widths
  5/2/1 and two fixed layers, retaining the original one-mip regression.
  New exact FP32-bit checks cover signed/omitted offsets, half-mip
  blends, negative/upper LOD and sampler clamps at mip 1, with waves
  4/8, guards and repeat 2; this does not change other ordinary
  shapes, materialize bound/static
  descriptors or establish owned D3D12 rendering.
  Heap-backed FP32 3D `SampleLevel` now also compiles in any stage, including
  native AOT, with default samplers, three-component U/V/W coordinates,
  FP32 LOD, constant signed XYZ offsets and scalar/vector results up to
  the declared texel width (at most four). Explicit LOD survives with six
  zero derivatives/bias and unchanged sampler LOD behavior. Native SDK
  DXC bindless 3D shaders execute through normal/reference/AOT paths
  with three rectangular mips of extents 4x2x2, 2x1x1 and 1x1x1,
  retaining the original one-mip regression. New exact FP32-bit checks
  cover shrinking depth, signed/omitted XYZ offsets, half-mip blends,
  negative/upper LOD and sampler clamps at mip 1, with waves 4/8,
  guards and repeat 2.
  Malformed shapes, non-heap handles, comparison samplers and dynamic/
  undefined/poison or partially undefined offsets remain unsupported.
  This does not enable non-fragment implicit 3D sampling or non-32-bit 3D loading,
  change other ordinary shapes, materialize bound/static descriptors or
  establish owned-device execution.
  Heap-backed FP32 3D `Load` now compiles in any stage, including native
  AOT, with three-component i32 XYZ coordinates, an i32 mip, constant
  signed XYZ offsets and scalar/vector results up to the declared texel
  width (at most four). Offsets are added to integer coordinates without
  overflow flags; the volume-load helper uses explicit mip, sample zero
  and no sampler/derivatives. Native DXIL's all-undefined omitted 3D offset
  raises to zero; partially undefined offsets stay rejected.
  Negative/out-of-range coordinates and
  invalid mips read zero with all three per-mip extents checked. Native
  SDK DXC bindless shaders execute through normal/reference/AOT paths.
  Malformed shapes, non-heap handles, non-32-bit texels and dynamic/undefined/
  poison offsets stay unsupported; all three paths reject dynamic offsets.
  Other loading/sampling, SPIR-V lowering, artifact ABI 8 and argument
  layouts remain unchanged. No bound/static descriptors or owned D3D12
  execution are added.
  Heap-backed signed/unsigned i32 3D `Load` now also compiles in any stage,
  including native AOT, with the same coordinate/mip/offset rules and
  scalar/vector widths one through four. Result scalar types must match
  the texel; exact integer bits are preserved without FP32 conversions.
  Invalid mips and lower/upper per-mip XYZ bounds read zero. Native SDK
  DXC `Texture3D<int4>` and `Texture3D<uint4>` shaders execute through
  normal/reference/AOT paths on one and three real mips. Mixed scalar
  types, non-32-bit texels, too-wide results, non-heap handles and dynamic/undefined/poison
  offsets remain unsupported. Existing FP32 loads, sampling, other
  dimensions, SPIR-V lowering, artifact ABI 8 and argument layouts are
  unchanged; SPIR-V integer loads share the runtime bounds correction.
  No bound/static descriptors or owned D3D12 execution are added.
  Heap-backed FP32 1D/array `Load` now compiles in any stage, including
  native AOT, with scalar i32 X or paired `(X,layer)` coordinates, an i32
  mip, constant signed scalar spatial offsets and scalar/vector FP32
  results up to the declared texel width (one through four). Wrapping
  adds affect only X, not layers. Existing helpers use explicit mip and
  sample zero without samplers/derivatives. Native omitted undefined/
  poison spatial offsets normalize to zero; canonical dynamic/undefined/
  poison offsets reject. Invalid mips, per-mip X bounds and independent
  layer bounds return zero. Native SDK DXC shaders execute through normal/
  reference/AOT paths on one and three real mips.
  Non-32-bit 1D/array loads remain unsupported.
  Existing 3D loads, sampling, SPIR-V lowering, artifact ABI 8 and argument
  layouts are unchanged; SPIR-V FP32 loads share the runtime bounds
  correction. No bound/static descriptors or owned D3D12 execution are added.
  Heap-backed signed/unsigned i32 1D/array `Load` now also compiles in any
  stage, including native AOT, with the same coordinate/mip/offset rules
  and widths one through four. Matching result/texel scalar types and
  integer helpers preserve exact bits without FP32 conversions. Invalid
  mips, per-mip X bounds and independent layer bounds read zero. Native
  SDK DXC plain/array `int4`/`uint4` shaders execute through normal/
  reference/AOT paths on one and three real mips. Mixed scalar types,
  non-32-bit texels, too-wide results, non-heap handles and dynamic/undefined/poison offsets
  remain unsupported. Existing FP32 loads, 3D loads, sampling, SPIR-V
  lowering, artifact ABI 8 and argument layouts are unchanged; SPIR-V
  integer users share the runtime bounds correction. No bound/static
  descriptors or owned D3D12 execution are added.
  Heap-backed FP32 2D/array `Load` now compiles in any stage, including
  native AOT, with paired `(X,Y)` or three-component `(X,Y,layer)` i32
  coordinates, an i32 mip, constant signed XY offsets and scalar/vector
  results up to the declared FP32 texel width (one through four).
  Wrapping adds offset only X/Y; layers remain separate. Existing helpers
  use sample zero without samplers/derivatives. The old zero-offset-only
  fallback is removed. Malformed coordinates, padded array offsets,
  integer/non-FP32 texels, too-wide results, non-heap handles and dynamic/
  undefined/poison or partial offsets reject. Native omitted undefined/
  poison XY offsets raise to zero; canonical offsets remain strict.
  Invalid mips, per-mip spatial bounds and independent layer bounds read
  zero. Native SDK DXC plain/array shaders execute through normal/
  reference/AOT paths on one and two real mips.
  Existing 1D/3D loads, sampling,
  SPIR-V lowering, artifact ABI 8 and argument layouts are unchanged;
  SPIR-V FP32 users share the runtime bounds correction. No bound/static
  descriptors or owned D3D12 execution are added.
  Heap-backed signed/unsigned i32 2D/array `Load` now also compiles in
  any stage, including native AOT, with the same coordinate/mip/offset
  rules and widths one through four. Matching result/texel scalar types
  and integer helpers preserve exact bits without FP32 conversions.
  Invalid mips, per-mip X/Y bounds and independent layer bounds read
  zero. Native SDK DXC plain/array `int4`/`uint4` shaders execute through
  normal/reference/AOT paths on one and two real mips. Mixed scalar types, non-32-bit
  texels, too-wide results, non-heap handles and dynamic/undefined/
  poison or partial offsets reject. Existing FP32 loads, 1D/3D loads,
  sampling, SPIR-V lowering, artifact ABI 8 and argument layouts are
  unchanged; SPIR-V integer users share the runtime bounds correction.
  No bound/static descriptors or owned D3D12 execution are added.
  Heap-backed FP32 `Texture2DMS::Load`/`Texture2DMSArray::Load` now also
  compiles in any stage, including native AOT. Native opcode 66 raises
  `dx.MSTexture` loads to `llvm.dx.resource.load.ms`, preserving the i32
  sample index rather than treating it as a mip. Shape-matched integer
  coordinates, constant signed XY offsets and scalar/vector FP32 results
  up to the declared width (one through four) use existing 2D/array helpers
  with mip zero, the live sample and separate layers. Invalid samples
  return zero without aliasing another texel/row/layer. SDK DXC FP32
  plain/array shaders pass import/raising and AOT compilation; integer MS
  execution is covered below. MS sampling and
  dynamic/undefined/poison or partial offsets remain unsupported.
  Runner YAML now prepares MS images, and native MS execution passes
  normal/reference/AOT-object tests. Artifact ABI 8 and argument layouts
  are unchanged.
  No bound/static descriptors or owned D3D12 execution are added.
  Heap-backed signed/unsigned i32 MS `Load` now also compiles in any stage,
  including native AOT. The same coordinate/sample/offset rules and
  widths one through four use integer helpers without FP32 conversions.
  Calls pass mip zero, live samples and separate layers. Invalid samples
  read zero without aliasing another texel/row/layer. SDK DXC plain/array
  `int4`/`uint4` shaders pass native import/raising and
  normal/reference/AOT-object execution with runner MS YAML. Mixed scalar types,
  non-32-bit texels, too-wide results, non-heap handles and dynamic/
  undefined/poison or partial offsets remain rejected. Native raising,
  FP32 MS/single-sample loads, sampling, artifact ABI 8 and argument
  layouts are unchanged; no bound/static descriptors or owned D3D12
  execution are added.
  Runner MS YAML accepts `2d-ms`/`2d-ms-array` with a positive
  `sample-count` and checked sample-interleaved layouts on one mip.
  Native FP32/SINT/UINT fixtures distinguish samples, rows/layers,
  components, signed/omitted offsets, bounds, partial waves, guards
  and repeats. This remains CPU execution, not owned D3D12 execution
  or an MSAA-quality/support advertisement.
  Non-MS runner YAML also accepts `mip-levels`, default 1, in [1, 32],
  using checked mip-major layouts with shrinking spatial extents/3D
  depth and fixed array/cube layers. Native FP32/SINT/UINT 2D/plain-array
  three-mip fixtures execute through normal/reference/AOT-object paths,
  with extents 5x3/2x1/1x1 and two fixed layers. Original one/two-mip
  regressions remain. Exact negative/fractional FP32 and integer bits
  check signed/omitted offsets, first/last texels/components and all
  lower/upper X/Y/layer/mip bounds, with normal waves 4/8, five lanes,
  guards and repeat 2;
  native 1D/plain-array and 3D fixtures also execute three real mips.
  Widths 5/2/1 retain fixed layers, rectangular 3D depth shrinks and
  signed offsets select first/last texels/components. Invalid X/layers/
  mips and upper XYZ bounds, exact integer bits, waves 4/8, partial
  waves, guards and repeat 2 are checked, with one-mip coverage retained;
  canonical fixtures add three non-power-of-two mips, mip-31 bounds,
  zero-fill and cube half-mip blending. See
  [the runner schema](feme-run.md). This does not change CPU lowering,
  artifact ABI 8, argument layouts or owned D3D12 execution.
  Heap-backed FP32 3D implicit `Sample` now lowers in fragment stages with
  default samplers, three-component U/V/W coordinates, constant signed XYZ
  offsets and scalar/vector results up to the declared texel width (at most
  four). Six real coarse spatial derivatives drive LOD with zero shader
  bias and no shader MinLod clamp. Native SDK DXC pixel shaders pass import/
  raising; real shader-clamped variants remain unraised. Non-fragment
  calls and malformed/dynamic/undefined/poison offsets stay unsupported;
  normal/reference/AOT compute admission rejects even valid constant-offset
  calls. Non-32-bit DXIL 3D loading, other sampling, SPIR-V, artifact ABI 8 and argument
  layouts are unchanged. No bound/static descriptors or owned D3D12
  execution are added.
  Heap-backed FP32 1D/array implicit `Sample` now lowers in fragment stages
  with default samplers, scalar U or `(U,layer)`, constant signed scalar
  offsets and scalar/vector results up to the declared texel width (at
  most four). Real coarse U derivatives drive LOD with zero shader bias;
  array layers remain undifferentiated. Native SDK DXC plain/array pixel
  shaders pass import/raising; real shader-clamped variants remain unraised.
  Non-fragment calls and malformed/dynamic/undefined/poison offsets remain
  unsupported; normal/reference/AOT compute admission rejects even valid
  constant-offset calls. This does not change other ordinary shapes
  or materialize bound/static descriptors.
  Owned D3D12 rendering remains open.
  FP32 `SampleGrad` now has checked opcode-63 import/raising for 1D/array,
  2D/array, 3D and cube/array textures, with scalar/vector FP32 texels up to
  four components and default samplers. Coordinates/layers, spatial gradients,
  signed/dynamic spatial offsets and real zero/nonzero/dynamic MinLod clamps
  survive; gradient width excludes array layers. Undefined/poison clamps
  select the plain intrinsic. Operand/result/use guards reject malformed
  calls without another sample. Seven-shape roundtrips, raw guards,
  idempotence and native DXC plain/clamped import pass. Heap-backed FP32
  1D/2D/3D/cube gradients (including arrays) execute in any stage, including
  native AOT, with default samplers, shape-matched coordinates/gradients and
  optional shader clamps. 1D/2D/3D use constant signed offsets; cubes require
  unused zero/undef/poison scalar offsets. Scalar/vector results cannot
  exceed the declared texel width (at most four). Supplied gradients drive
  existing LOD/projection helpers without synthesized derivatives or an
  LOD-zero approximation. Array elements remain coordinate-only, excluded
  from scalar U, two-component U/V or three-component raw-direction gradients.
  1D gradients/offsets are scalar; 3D coordinates, gradients and offsets
  have three U/V/W components. Actual SDK DXC SM6.6 2D/plain-array and
  3D gradient shaders execute three real mips through normal waves 4/8,
  reference and ARM64 AOT-object paths. Live U/dX, negative V/dY and
  3D W/dX distinguish mips 0/1/2; shader MinLod floors 0.5/1.5 check
  exact FP32-bit half-mip blends. Independent plain-gradient samples
  check sampler clamps at mip 1, with signed/omitted offsets, layers/
  depth, components, five lanes, guards and repeat 2. Native 1D/plain-
  array and cube/plain-array gradient shaders also execute three real
  mips through the same CPU paths. Scalar 1D gradients/offsets, negative
  U/dY, scalar cube texels/results, all faces/elements and face-local
  tangent projection on both screen axes are checked. Joint cube
  direction/gradient scaling by one/two preserves LOD; exact-bit shader
  half-mip blends and sampler clamps at mip 1 pass with five/thirteen
  lanes, guards and repeat 2. Native gradient execution evidence now
  spans all seven raised shapes.
  All seven raised shapes have bounded CPU
  execution; malformed or non-heap calls remain explicitly unsupported.
  This does not
  materialize descriptors or establish owned D3D12 rendering.
  General descriptor-backed CBVs and cbuffer arrays remain unsupported.
  This is binding preflight, not descriptor/table preparation or full
  shader/root compatibility. This applies to DXIL and original DXBC input
  and native AOT compilation. Other targets reject this option. The default
  inferred mode is unchanged.

  Bound raw/typed buffer normalization preserves separate SRV/UAV register
  namespaces in both modes, including coincident register numbers. AOT
  reflection uses artifact ABI 8 with a namespace per bound range;
  version-7 objects must be recompiled. Shader argument layouts are unchanged.

* `-O0`, `-O1`, `-O2`, `-O3`

  Optimization level for FeMe's IR optimization pipeline (see
  `feme::OptimizerPipeline`), run after import/raising and before
  retargeting/codegen. Mirrors `clang`/`opt`'s levels of the same name;
  defaults to `-O0` ("disable as many optimizations as possible") when no
  `-O` flag is given.

* `-Od`

  Alias for `-O0`, matching `clang-cl`/DXC's spelling for "disable
  optimizations".

* `-help`, `-h`

  Display available options and exit.

* `-version`

  Display the version of `feme` and exit.

## EXAMPLES

```shell
# Translate a DXIL module to SPIR-V.
feme --target=spirv input.dxil -o output.spv

# Re-emit a DXIL module (e.g. after external re-optimization).
feme --target=dxil input.dxil -o output.dxil

# Translate a DXBC module to DXIL.
feme --target=dxil input.dxbc -o output.dxil

# Import SPIR-V and retarget it to an AMDGPU target.
feme --target=amdgcn-amd-amdhsa input.spv -o output.o
```

## EXIT STATUS

`feme` returns 0 on success, and a non-zero value if the command line could
not be parsed, or if translation fails.
