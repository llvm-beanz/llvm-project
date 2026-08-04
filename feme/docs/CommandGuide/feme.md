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
input file's contents; currently detected formats are `dxil` and `spirv`
(DXBC import is not yet implemented -- see the Roadmap / Milestones section
of [../Design.md](../Design.md) -- so DXBC input, like any input whose
format cannot be detected, is rejected with a diagnostic). `--target` may
name `dxil`, `spirv` (re-serializing back to that format via its own LLVM
backend), or any other LLVM target triple registered with the
`TargetRegistry` (e.g. `amdgcn-amd-amdhsa`) for real-ISA retargeting.

Current limitations. Retargeting a DXIL input to any target first requires
undoing its `dx.op.*` calling convention and recovering the shader model and
entry point information from its `dx.*` metadata; retargeting to SPIR-V or
`amdgcn-*` additionally requires re-expressing the result in that target's
own intrinsics and resource conventions. All of these passes are
deliberately incremental (see the DXIL, "Raised LLVM IR -> AMDGPU", and
"Raised LLVM IR -> SPIR-V" sections of [../Design.md](../Design.md)): typed
buffers (`Buffer`/`RWBuffer`) are covered, but textures, samplers, raw and
structured buffer accesses, cbuffer loads, and shader input/output signature
ops are not, so a shader using those will fail at this stage rather than at
any point specific to `feme`'s own Driver/CLI logic.

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

# Translate a DXBC module to DXIL (not yet implemented).
feme --target=dxil input.dxbc -o output.dxil

# Import SPIR-V and retarget it to an AMDGPU target.
feme --target=amdgcn-amd-amdhsa input.spv -o output.o
```

## EXIT STATUS

`feme` returns 0 on success, and a non-zero value if the command line could
not be parsed, or if translation fails.
