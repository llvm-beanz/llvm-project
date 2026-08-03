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
independent, human-readable, diffable binary test fixtures for the DXBC
importer, without depending on any code that importer's own tests
validate. See the "`dxbc-as`: a standalone DXBC assembler" section of
[../Design.md](../Design.md) for the full rationale.

`dxbc-as` covers the whole SM4/SM5 instruction set: every
`D3D10_SB_OPCODE_TYPE` value has a mnemonic in
`feme/include/feme/DXBC/Assembler/Opcodes.def`.

## SYNTAX

Each line is one instruction or directive; `//` and `;` start a comment.

### Instructions

```
<mnemonic>[_sat] [<modifier>...] [<operand>[, <operand>...]]
```

Opcode-specific control fields are spelled as part of the mnemonic rather
than as positional keywords, so `callc_z`/`callc_nz`,
`resinfo`/`resinfo_rcp`/`resinfo_uint`, `dcl_sampler`/
`dcl_sampler_comparison` and `dcl_resource_texture2d`/
`dcl_resource_texture3d` are separate mnemonics for the same opcode.

The modifiers, all optional and accepted in any order, are:

- `precise(<components>)` — the per-component precise mask, e.g.
  `precise(xy)`.
- `aoffimmi(<u>, <v>, <w>)` — immediate texel address offsets, each in
  `[-8, 7]`.
- `resource_dim(<dimension>[, <stride>])` and
  `resource_return_type(<x>, <y>, <z>, <w>)` — the SM5.1 extended opcode
  tokens.
- `globallyCoherent`, `rasterizerOrdered`, `hasOrderPreservingCounter` —
  UAV access flags, on `dcl_uav_*` only.

### Operands

```
[-][|]<storage class>[<index>]['['<index>']'...][.<components>][{<modifier>,...}][|]
```

The storage class is the same keyword the `dxsa` dialect prints (`r`, `v`,
`o`, `cb`, `icb`, `x`, `t`, `s`, `u`, `g`, `label`, `null`, `vPrim`,
`vThreadID`, ...). Indices may be immediate (`cb0[3]`), relative
(`v[r1.x]`) or both (`cb0[3 + r1.x]`). Immediates are written `l(...)`
(32-bit) and `d(...)` (64-bit), with one or four components; an integer or
hexadecimal literal keeps its value while a literal spelled with a `.` or
an exponent is converted to its float bit pattern.

A component suffix is interpreted by operand position, exactly as `fxc`
disassembly spells it: on a destination `.x` is a one-bit write mask, on a
source it selects a single component, and four letters on a source are a
swizzle. The `{...}` modifier list overrides that and carries what the
bare syntax cannot express: `{mask}`/`{swizzle}`/`{select1}` force a
selection mode, `{comp0}`/`{comp1}`/`{comp4}` force a component count, and
`{min16f}`/`{min2_8f}`/`{min16i}`/`{min16u}`/`{nonuniform}` set the
extended operand token's minimum-precision and non-uniform bits.

### Directives

- `.shader_model <pixel|vertex|geometry|hull|domain|compute> <major>
  <minor>` — emit a program header. Without it the output is a bare
  instruction sequence, which is what a `DXContainer`'s `SHEX` part
  contains once its header is stripped.
- `.dword <value>[, <value>...]` — emit tokens verbatim, for fixtures that
  must be malformed (an unknown opcode, a wrong instruction length, a
  corrupted operand type) to exercise an importer's error paths.

## OPTIONS

- `<input file>` — the DXBC assembly source to assemble, or `-` for stdin
  (the default).
- `-o <file>` — output filename, or `-` for stdout (the default).
- `--emit=<kind>` — what to produce:
  - `binary` (default) — raw DXBC tokenized shader bytecode.
  - `container` — the bytecode wrapped in a minimal `DXContainer` (a single
    `SHEX` part).
  - `asm` — re-prints the parsed input as normalized assembly text,
    primarily to sanity-check how `dxbc-as` understood an input without
    inspecting the binary encoding. Re-assembling this output produces the
    same bytecode as assembling the original.

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
dxbc-as %s | feme-translate --import-dxsa-bin -
```

## EXIT STATUS

`dxbc-as` returns 0 on success, and a non-zero value if the input fails to
parse (e.g. an unknown mnemonic, wrong operand count, or a `_sat` suffix on
a non-floating-point instruction) or a parsed instruction cannot be
encoded. Diagnostics include the offending line and column; malformed input
is always reported as an error, never a crash (`dxbc-as-fuzzer`, see
[dxbc-as-fuzzer.md](dxbc-as-fuzzer.md), exists specifically to check this).
