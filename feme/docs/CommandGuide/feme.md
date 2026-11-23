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
  Heap-backed FP32 1D/2D/cube biased sampling, including arrays, now executes
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
  1D offsets reject. Ordinary DXIL 1D sampling/loading is unchanged; 3D
  biased execution remains unsupported.
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
