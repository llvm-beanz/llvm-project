# `feme-opt` — FeMe pass-pipeline testing driver

## SYNOPSIS

```shell
feme-opt [options] <input file>
```

## DESCRIPTION

`feme-opt` is an `mlir-opt`/`opt`-style pass-pipeline driver, built on
`MlirOptMain`/`PassPipelineCLParser`. It lets `lit`+`FileCheck` tests
exercise a single FeMe pass or conversion in isolation on textual MLIR/LLVM
IR — e.g. running just the DXIL "op raising" pass on hand-written `dx.op.*`
IR and checking the raised output, or running just the `dxsa` → LLVM IR
lowering pass on hand-written `dxsa` dialect text — without needing a binary
importer in the loop at all. This is the primary way FeMe's own passes are
tested, since most pass bugs have nothing to do with binary parsing. See the
"Testing Tools" section of [../Design.md](../Design.md) for the full
rationale.

`feme-opt` is a **testing-only** entry point: unlike `feme` itself, it may
use `llvm::cl::opt` (matching `mlir-opt` convention), per the exception
carved out for narrowly-scoped testing tools in the "Core Architectural
Principle: No Global State" section of [../Design.md](../Design.md). It is
not intended for end users.

`feme-opt` is currently a scaffolding-only skeleton for its MLIR pass
pipeline mode (roadmap step 1 in [../Design.md](../Design.md)): it registers
all standard MLIR dialects and passes, but no FeMe-specific dialects or
passes yet. Its LLVM IR pass pipeline mode (`--llvm`, see below) does
register FeMe's own LLVM IR passes, starting with
`feme::dxil::OpRaisingPass` (`feme-dxil-raise-ops`).

## MODES

`feme-opt` has two modes, since FeMe passes come in two shapes (MLIR
operations vs. plain `llvm::Module`, see the "Testing Tools" section of
[../Design.md](../Design.md)):

- **MLIR mode** (default): behaves exactly like `mlir-opt`, built on
  `MlirOptMain`/`PassPipelineCLParser`.
- **LLVM IR mode** (`feme-opt --llvm ...`): a small, `opt`-style new-pass-
  manager driver. `--llvm` must be the first argument. Accepts a
  `-passes=<pipeline>` textual pass pipeline (same syntax as `opt`'s
  `-passes=`), `-S` to print textual LLVM IR instead of bitcode, and `-o
  <file>` for the output path (`-` for stdout, the default).

  ```shell
  feme-opt --llvm -passes=feme-dxil-raise-ops -S input.ll
  ```

  The FeMe LLVM IR passes currently registered under their `-passes=` names
  are:

  | Name | Pass |
  |---|---|
  | `feme-dxil-raise-ops` | `feme::dxil::OpRaisingPass` |
  | `feme-dxil-raise-metadata` | `feme::dxil::MetadataRaisingPass` |
  | `feme-dxil-expand-intrinsics` | `feme::dxil::IntrinsicExpansionPass` |
  | `feme-spirv-lower-raised` | `feme::spirv::RaisedLoweringPass` |
  | `feme-amdgpu-lower-raised` | `feme::amdgpu::RaisedLoweringPass` |
  | `feme-amdgpu-lower-resources` | `feme::amdgpu::ResourceLoweringPass` |

## OPTIONS

`feme-opt`'s MLIR mode accepts the full set of options provided by MLIR's
`MlirOptMain`/`PassPipelineCLParser` (e.g. `--pass-pipeline=<pipeline>` to
run an explicit pass pipeline, `--mlir-print-debuginfo`,
`--allow-unregistered-dialect`, and the many other generic MLIR/LLVM
options). Run `feme-opt --help` for the full, current list; only
FeMe-specific behavior is documented here. Its LLVM IR mode accepts only the
small set of options listed above under MODES.

## EXAMPLES

```shell
# Run a named pass pipeline on a textual MLIR/dxsa input, and check the
# result with FileCheck.
feme-opt --pass-pipeline='builtin.module(some-feme-pass)' input.mlir | FileCheck input.mlir

# Raise DXIL's dx.op.* calls back to LLVM/llvm.dx.* intrinsic calls.
feme-opt --llvm -passes=feme-dxil-raise-ops -S input.ll | FileCheck input.ll
```


## EXIT STATUS

`feme-opt` returns 0 on success, and a non-zero value if parsing the input
or running the requested pass pipeline fails.
