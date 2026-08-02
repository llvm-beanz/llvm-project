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

`feme` is currently under initial development (see the Roadmap / Milestones
section of [../Design.md](../Design.md)) and does not yet implement any
translation; invoking it with any arguments other than `--help`/`--version`
currently reports an error.

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

Once translation support lands (see the roadmap in
[../Design.md](../Design.md)), `feme` is expected to be invoked as:

```shell
# Translate a DXIL module to SPIR-V.
feme --from=dxil --to=spirv input.dxil -o output.spv

# Translate a DXBC module to DXIL.
feme --from=dxbc --to=dxil input.dxbc -o output.dxil

# Import SPIR-V and retarget it to an AMDGPU target.
feme --from=spirv --target=amdgcn-amd-amdhsa input.spv -o output.o
```

## EXIT STATUS

`feme` returns 0 on success, and a non-zero value if the command line could
not be parsed, or if translation fails.
