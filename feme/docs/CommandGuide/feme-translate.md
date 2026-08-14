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
detects its input format automatically and resolves a full `Driver`-level
`--target` chain, only producing final binary/ISA output, while
`feme-translate` stops at a single stage and can emit human-readable
intermediate IR. See the "Testing Tools" section of
[../Design.md](../Design.md) for the full rationale.

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

* `--import-dxbc`

  Imports a legacy DXBC module via `feme::DXBCImporter`, producing the MLIR
  `dxsa` dialect's textual form. Accepts a full `DXContainer` carrying an
  `SHEX`/`SHDR` part (see the "DXBC" section of [../Design.md](../Design.md));
  unlike `--import-dxil`, DXBC has no bare-bytecode-only encoding to also
  accept, since a real DXBC module is never distributed outside a
  container. Complements `--import-dxsa-bin` (below), which imports the
  bare tokenized bytecode `dxbc-as` emits (with no container).

* `--import-dxsa-bin`

  Imports raw (container-less) DXBC tokenized shader bytecode via
  `feme::dxsa::deserialize`, producing the MLIR `dxsa` dialect's textual
  form. This is the flag `dxbc-as`'s default `--emit=binary` output pipes
  into (see [dxbc-as.md](dxbc-as.md)), and is what most DXBC `lit` tests
  use for a diffable, human-readable fixture instead of a full
  `DXContainer`.

* `--dxsa-to-llvmir`

  Translates a `dxsa` dialect module (as produced by `--import-dxbc` or
  `--import-dxsa-bin`) to DXIL-shaped LLVM IR via
  `feme::dxsa::translateToLLVMIR` — the DXBC → DXIL edge of the Translation
  Matrix in [../Design.md](../Design.md). Accepts a `--dxbc-container=<path>`
  option naming a full `DXContainer` to read real `ISGN`/`OSGN` signature
  element names/types from, overriding the names this translation would
  otherwise synthesize from the input module's declarations (see "Building
  complete legacy DXBC containers for testing" in
  [../Design.md](../Design.md)).

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

Import a legacy DXBC `DXContainer` via `feme::DXBCImporter`, then translate
it to DXIL-shaped LLVM IR via `feme::dxsa::translateToLLVMIR`:

```shell
dxbc-as --emit=container input.dxasm -o input.dxbc
feme-translate --import-dxbc input.dxbc | feme-translate --dxsa-to-llvmir -
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
