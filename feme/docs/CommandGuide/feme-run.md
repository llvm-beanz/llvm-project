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
  buffers; typed buffers and textures require descriptor tables. Counter
  compatibility and descriptor promises are not validated by this check.
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
      data: [0, 1, 2, 3]         # little-endian uint32 words, one mip/layer
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
