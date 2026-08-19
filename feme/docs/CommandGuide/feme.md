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
covered, but samplers, raw and structured buffer accesses when retargeting
to `amdgcn-*` (typed buffer/texture/cbuffer accesses alone are covered
there; raw/structured buffers are not yet), texture sampling
(`Sample`/`SampleLevel` raise but have no `amdgcn-*`/NVPTX lowering yet),
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
