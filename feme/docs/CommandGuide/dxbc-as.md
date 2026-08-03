# `dxbc-as` — standalone DXBC assembler

## SYNOPSIS

```shell
dxbc-as [options] <input file>
```

## DESCRIPTION

`dxbc-as` assembles the well-known Microsoft/`fxc` DXBC disassembly textual
syntax (the mnemonic-based SM4/SM5 shader assembly produced by
`fxc /dumpbin`/`D3DDisassemble`, e.g. `dcl_resource_texture2d`,
`sample r0.xyzw, v1.xyxx, t0.xyzw, s0`, `mov`, `add`, `ret`) into raw DXBC
tokenized shader bytecode, optionally wrapped in a `DXContainer`. It follows
a traditional compiler pipeline — lex ➜ parse ➜ encode — implemented
entirely under `feme/lib/DXBC/Assembler` and exposed through
`feme/include/feme/DXBC/Assembler`.

`dxbc-as` has **no dependency on MLIR, the `dxsa` dialect, or
`feme::Context`**: it is a plain LLVM-style lexer/parser + binary encoder
(comparable in spirit to `llvm-mc`), and exists purely to produce
independent, human-readable, diffable binary test fixtures for a future
DXBC importer, without depending on any code that importer's own tests
validate. See the "`dxbc-as`: a standalone DXBC assembler" section of
[../Design.md](../Design.md) for the full rationale.

`dxbc-as` supports a deliberately curated, representative subset of the
real SM4/SM5 instruction set (see
`feme/include/feme/DXBC/Assembler/Opcodes.def` for the exact list) — every
operand-encoding shape the format uses (plain ALU, texture
sampling/loads, declarations, no-operand, conditional) is covered, but this
is not an exhaustive reimplementation of the ~200-opcode real ISA. Adding
more mnemonics later is purely additive.

## OPTIONS

- `<input file>` — the DXBC assembly source to assemble, or `-` for stdin
  (the default).
- `-o <file>` — output filename, or `-` for stdout (the default).
- `--emit=<kind>` — what to produce:
  - `binary` (default) — raw DXBC tokenized shader bytecode.
  - `container` — the bytecode wrapped in a minimal `DXContainer` (a single
    `SHEX` part).
  - `asm` — re-prints the parsed input as normalized assembly text (e.g.
    default write masks/swizzles made explicit), primarily to sanity-check
    how `dxbc-as` understood an input without inspecting the binary
    encoding.
- `--shader-kind=<pixel|vertex|compute>` — shader stage to declare in the
  program's version token (default `pixel`).

## EXAMPLES

```shell
# Assemble to raw tokenized bytecode.
dxbc-as input.dxasm -o out.dxbc

# Assemble to a full DXContainer.
dxbc-as --emit=container input.dxasm -o out.dxcontainer

# Sanity-check how dxbc-as parsed an input.
dxbc-as --emit=asm input.dxasm

# Typical `lit` usage: build a binary fixture at test time instead of
# checking one in (see "Avoiding binary test fixtures" in
# ../Design.md), then feed it to a future DXBC importer.
dxbc-as %s | feme-translate --import-dxbc=-
```

## EXIT STATUS

`dxbc-as` returns 0 on success, and a non-zero value if the input fails to
parse (e.g. an unknown mnemonic, wrong operand count, or a `_sat` suffix on
a non-floating-point instruction) or a parsed instruction cannot be
encoded. Diagnostics include the offending line and column; malformed input
is always reported as an error, never a crash (`dxbc-as-fuzzer`, see
[dxbc-as-fuzzer.md](dxbc-as-fuzzer.md), exists specifically to check this).
