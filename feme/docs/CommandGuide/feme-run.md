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
  Heap-backed FP32 2D/cube biased sampling, including arrays, now executes
  in the separate compiled fragment-stage path, with real spatial/direction
  derivatives, implicit LOD, bias, supported offsets and optional shader MinLod clamps.
  Scalar/vector results up to the declared texel width (at most four) narrow
  the shared four-component result. Mip/offset/component selection,
  divergent bias/clamps, helpers and guarded partial waves are tested.
  This compute runner still rejects both biased intrinsics in normal/
  reference/AOT dispatch. Other shapes/stages, non-heap handles, comparison
  samplers and dynamic/undefined spatial offsets also reject explicitly; no
  descriptor materialization or owned-device execution is enabled.
  Biased arrays use FP32 (U, V, layer); only U/V are differentiated and
  the two offsets remain spatial. Compiled array tests check divergent
  layers/bias/clamps, layer/mip/offset/component selection and guarded
  partial waves. Missing-layer coordinates still reject explicitly.
  Cubes use six raw-XYZ derivatives for face-projected implicit LOD; a
  separate cube-array element is never differentiated. Unused scalar offsets
  must be poison/undef or zero. Compiled tests check all faces, both elements,
  biased/clamped mip selection and components with guarded partial waves.
  Nonzero/dynamic cube offsets and 1D/3D biased execution remain unsupported.
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
