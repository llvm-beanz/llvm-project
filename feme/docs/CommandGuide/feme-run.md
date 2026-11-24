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
  Heap-backed FP32 1D/array `SampleLevel` now executes in any stage through
  normal/reference/AOT paths with default samplers, scalar U or `(U,layer)`
  coordinates, constant signed scalar offsets and scalar/vector results
  up to the declared texel width (at most four). Explicit LOD is preserved
  with zero derivatives/bias and unchanged sampler LOD behavior.
  Compiled-stage tests verify multi-mip/layer selection and sampler clamps;
  native SDK DXC bindless 1D/array runner tests cover signed offsets,
  components, divergent layers/LODs, partial waves, guards and repeats on
  one mip. Runner image YAML still prepares one mip. Malformed shapes,
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
  clamps; native SDK DXC bindless 3D runner tests cover signed XYZ offsets,
  components, divergent coordinates/LODs, partial waves, guards and repeats
  on one mip. Runner image YAML still prepares one mip. Malformed shapes,
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
  runner YAML still prepares one mip. Malformed shapes, non-heap handles,
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
  five lanes, guards and repeats on one mip; runner YAML still prepares
  one mip. Mixed scalar types, non-32-bit texels, too-wide results,
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
  mip; runner YAML still prepares one mip. Dynamic-offset admission rejects
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
  lanes, guards and repeats on one mip; runner YAML still prepares one
  mip. Dynamic-offset admission rejects in all three paths. Mixed scalar
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
  one mip; runner YAML still prepares one mip. Dynamic-offset admission
  rejects in all three paths. Runner MS preparation remains open.
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
  components, invalid mips, five lanes, guards and repeats on one mip;
  runner YAML still prepares one mip. Dynamic-offset admission rejects
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
  shaders pass import/raising and AOT compilation only. **Runner YAML
  still rejects MS image preparation**, so native MS execution through
  this runner is not claimed. Integer MS execution is covered below.
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
  import/raising and AOT compilation only. **Runner YAML still rejects
  MS image preparation**; native MS execution through this runner is not
  claimed. Dynamic integer offsets fail normal/reference/AOT admission.
  Mixed scalar types, non-32-bit texels, too-wide results, non-heap handles
  and undefined/poison or partial offsets remain rejected. Native raising,
  FP32 MS/single-sample loads, sampling, artifact ABI 8 and argument
  layouts are unchanged; no bound/static descriptors or owned D3D12
  execution are added.
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
  element selection; runner image YAML currently prepares one mip, so its
  execution fixtures cover faces/elements, offsets, components, partial
  waves and repeats. 1D gradients/offsets are scalar; 3D coordinates,
  gradients and offsets have three U/V/W components. All seven raised
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
      dimension: 1d              # 1d/1d-array/2d/2d-array/cube/cube-array/3d
      extent: [4, 1]
      format: r32g32b32a32_float
      data: [0, 1, 2, 3]         # little-endian uint32 words, one mip, layer-major
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
