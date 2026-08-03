# `feme` — FeMe: FrontEnd for the MiddleEnd

## SYNOPSIS

```shell
feme [options] <input file>
```

## DESCRIPTION

`feme` is FeMe's primary, user-facing command line tool: given an input
shader/IR file, a source format, and a destination format or target ISA,
`feme` imports the input, translates it into FeMe's internal representation,
and retargets/exports it to the requested output — the same way Clang's
driver builds compile+assemble+link jobs from `-x`/`-o`/`--target` rather
than requiring callers to invoke each compilation stage by hand. See the
"Command Line Tool(s)" section of [../Design.md](../Design.md) for the full
design.

`feme` drives `feme::Driver` (see the "Driver" section of
[../Design.md](../Design.md)). Currently supported `--from` values are
`dxil` and `spirv` (DXBC import is not yet implemented -- see the Roadmap /
Milestones section of [../Design.md](../Design.md)); `--to`/`--target` may
each independently name `dxil`, `spirv` (re-serializing back to that format
via its own LLVM backend), or any other LLVM target triple registered with
the `TargetRegistry` (e.g. `amdgcn-amd-amdhsa`) for real-ISA retargeting.

Current limitations: retargeting a DXIL input to any target requires
`feme::dxil::OpRaisingPass` to first undo its `dx.op.*` calling convention,
and retargeting to `amdgcn-*` additionally requires
`feme::amdgpu::RaisedLoweringPass`; both passes are deliberately incremental
(see the DXIL and "Raised LLVM IR -> AMDGPU" sections of
[../Design.md](../Design.md)) and do not yet cover resource load/store or
input/output signature ops, so real shaders using those will fail at this
stage rather than at any point specific to `feme`'s own Driver/CLI logic.

## OPTIONS

* `--from=<format>`

  Input format to translate from (e.g. `dxil`, `dxbc`, `spirv`).

* `--to=<format>`

  Output format/target to translate to (e.g. `dxil`, `spirv`, or a target
  triple).

* `--target=<triple>`

  Target triple to retarget the translated module to.

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
feme --from=dxil --to=spirv input.dxil -o output.spv

# Re-emit a DXIL module (e.g. after external re-optimization).
feme --from=dxil --to=dxil input.dxil -o output.dxil

# Translate a DXBC module to DXIL (not yet implemented).
feme --from=dxbc --to=dxil input.dxbc -o output.dxil

# Import SPIR-V and retarget it to an AMDGPU target.
feme --from=spirv --target=amdgcn-amd-amdhsa input.spv -o output.o
```

## EXIT STATUS

`feme` returns 0 on success, and a non-zero value if the command line could
not be parsed, or if translation fails.
