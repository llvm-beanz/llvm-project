# `feme-translate` — FeMe import/export testing driver

## SYNOPSIS

```shell
feme-translate [options] <input file>
```

## DESCRIPTION

`feme-translate` is an `mlir-translate`-style tool that exposes each
format's `Importer`/`Exporter`, as well as MLIR/LLVM-IR-to-MLIR/LLVM-IR
`feme::Translator`s, as individual translation flags, for testing a single
import/export/translation stage in isolation with textual (not
final-binary-ISA) output. This is distinct from `feme` itself: `feme`
resolves a full `Driver`-level `--from`/`--to`/`--target` chain and only
produces final binary/ISA output, while `feme-translate` stops at a single
stage and can emit human-readable intermediate IR. See the "Testing Tools"
section of [../Design.md](../Design.md) for the full rationale.

`feme-translate` is a **testing-only** entry point: unlike `feme` itself, it
may use `llvm::cl::opt` (matching `mlir-translate` convention), per the
exception carved out for narrowly-scoped testing tools in the "Core
Architectural Principle: No Global State" section of
[../Design.md](../Design.md). It is not intended for end users.

## OPTIONS

`feme-translate` registers all of MLIR's standard translations, plus the
FeMe-specific translations below. Run `feme-translate --help` for the full,
current list; only FeMe-specific translations are documented here.

* `--import-spirv`

  Imports a binary SPIR-V module via `feme::SPIRVImporter`, producing the
  MLIR `spirv` dialect's textual form. Complements MLIR's own
  `--deserialize-spirv`, but goes through FeMe's own importer/`Context`
  rather than the generic MLIR one.

* `--import-dxil`

  Imports a DXIL module via `feme::DXILImporter`, producing textual LLVM
  IR. Accepts either a raw (optionally wrapper-prefixed) LLVM bitcode file,
  or a `DXContainer` with an embedded DXIL bitcode part — see the "DXIL"
  section of [../Design.md](../Design.md) for why both encodings are
  accepted. Unlike `--import-spirv`, the output is plain LLVM IR text (via
  `llvm::Module::print`), not MLIR: DXIL import does not go through MLIR at
  all (see [../Design.md](../Design.md)).

* `--spirv-to-llvmir`

  Translates a `spirv` dialect module to LLVM IR via
  `feme::SPIRVToLLVMTranslator`. Lets FeMe's SPIR-V-to-LLVM-IR translation
  be `lit`/`FileCheck`-tested the same way as `--import-spirv`, rather than
  only via `gtest` (see the "Testing Strategy" section of
  [../Design.md](../Design.md)).

As with `mlir-translate`, useful generic options include
`--no-implicit-module` (do not wrap the input in an implicit top-level
module) and `-o <file>` (write output to `<file>` instead of standard
output).

## EXAMPLES

Round-trip a `spirv` dialect module through binary SPIR-V and back, via
`feme::SPIRVImporter`:

```shell
feme-translate --no-implicit-module --serialize-spirv input.mlir -o input.spv
feme-translate --import-spirv input.spv
```

Import a DXIL bitcode file or `DXContainer` via `feme::DXILImporter`:

```shell
feme-translate --import-dxil input.bc
feme-translate --import-dxil input.dxcontainer
```

Translate a `spirv` dialect module directly to LLVM IR via
`feme::SPIRVToLLVMTranslator`:

```shell
feme-translate --no-implicit-module --spirv-to-llvmir input.mlir
```

## EXIT STATUS

`feme-translate` returns 0 on success, and a non-zero value if parsing the
input or running the requested translation fails (e.g. a malformed binary
input, or a module that does not satisfy the chosen translation's
preconditions).
