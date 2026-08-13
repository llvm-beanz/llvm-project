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

**Scope (roadmap milestone 4, updated by milestone 10):** the input may
either already be idiomatic, raised LLVM IR (`.ll`/`.bc`), or a DXIL
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
- `--dxil-bind-register-resources`: for a DXIL input only, rewrites every
  register-bound resource (`llvm.dx.resource.handlefrombinding`) into a
  bindless heap access (`llvm.dx.resource.handlefromheap`) at the same
  index its register binding names, before handing the module to the FeMe
  CPU target. A testing-only bridge, *not* what the CPU target itself
  accepts (see "Root constants" in
  [../FeMeCPUDesign.md](../FeMeCPUDesign.md): every other register-bound
  handle is rejected) -- it exists because Clang's HLSL front end cannot
  yet emit `ResourceDescriptorHeap`/`SamplerDescriptorHeap` (bindless)
  access, so a real HLSL shader compiled by Clang today has no way to
  produce the bindless form the CPU target requires. See
  feme/test/Tools/feme-run/HLSL for HLSL compiled through this bridge.
- `--heap=<file>`: a YAML file describing the resource heap a dispatch runs
  against:

  ```yaml
  root-constants: [1, 2, 3, 4]   # optional: uint32 words
  resource-heap:
    - index: 0
      size: 64                  # bytes to allocate (zero-filled)
      data: [0, 1, 2, 3]        # optional: little-endian uint32 words
  ```

  Every entry becomes an unstructured, host-writable raw buffer
  (`ResourceKind::Raw`, `FEME_DESCRIPTOR_UAV` set); this is the same buffer
  a shader's `ResourceDescriptorHeap[index]` indexes.

## OUTPUT

After the dispatch completes, `feme-run` prints each heap entry's final
contents as `uint32` words, one line per entry, for `FileCheck` to match:

```
heap[0]: 0 1 2 3
```

## EXAMPLES

```shell
# Run a shader that writes its own dispatch thread id into a raw buffer,
# and check the buffer's contents afterwards.
feme-run --wave-size=4 --groups=1,1,1 --heap=heap.yaml shader.ll | FileCheck shader.ll

# Compile real HLSL to a DXIL DXContainer with Clang, and run it the same
# way (see feme/test/Tools/feme-run/HLSL for full examples).
clang -target dxil--shadermodel6.5-compute -c shader.hlsl -o shader.dxcontainer
feme-run --dxil-bind-register-resources --wave-size=4 --groups=1,1,1 \
    --heap=heap.yaml shader.dxcontainer | FileCheck shader.hlsl
```

## EXIT STATUS

`feme-run` returns 0 on success, and a non-zero value if parsing the input,
compiling the shader, or reading the heap file fails.
