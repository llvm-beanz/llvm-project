# `feme-spirv-import-fuzzer` — fuzzer for `feme::SPIRVImporter`

## SYNOPSIS

```shell
feme-spirv-import-fuzzer [libFuzzer options] [corpus dir...]
```

## DESCRIPTION

`feme-spirv-import-fuzzer` is a [libFuzzer](../../../llvm/docs/LibFuzzer.md)
harness for `feme::SPIRVImporter`. FeMe consumes externally-defined binary
formats (SPIR-V, DXIL, DXBC) supplied by untrusted sources at driver
runtime, so fuzzing each `Importer` is a v1 requirement (see the "Testing
Strategy" section of [../Design.md](../Design.md)), matching how other LLVM
binary-format parsers (e.g. `llvm-dis-fuzzer` for bitcode) are fuzzed.

Each fuzzer iteration constructs a fresh `feme::Context` and
`feme::SPIRVImporter`, feeds the fuzzer-provided bytes to
`SPIRVImporter::import` as a raw SPIR-V binary buffer, and discards any
resulting `Error` — the harness only needs the importer to never crash,
hang, or otherwise misbehave on malformed input, not to check any
particular output. A fresh `Context` per input matches how other in-tree
fuzzers (e.g. `llvm-dis-fuzzer`) use a fresh `LLVMContext` per input:
Importers must not rely on any state surviving across calls (see the "No
Global State" principle in [../Design.md](../Design.md)).

This tool is a testing-only entry point; it is not intended for end users
and has no CLI options of its own — all options come from libFuzzer itself.

## OPTIONS

`feme-spirv-import-fuzzer` accepts the standard set of
[libFuzzer options](../../../llvm/docs/LibFuzzer.md#options) (e.g. `-runs=N`,
`-max_len=N`, `-jobs=N`), plus one or more corpus directory arguments. Run
`feme-spirv-import-fuzzer -help=1` for the full, current list of libFuzzer
options.

## EXAMPLES

Run the fuzzer against a corpus directory, creating it first if it doesn't
already exist:

```shell
mkdir -p corpus
feme-spirv-import-fuzzer corpus
```

Run a fixed number of iterations, useful for quick regression checks in CI:

```shell
feme-spirv-import-fuzzer -runs=100000 corpus
```

Reproduce a specific crashing input found by a previous fuzzing run:

```shell
feme-spirv-import-fuzzer crash-<hash>
```

## EXIT STATUS

`feme-spirv-import-fuzzer` follows standard libFuzzer conventions: it runs
until stopped (or until `-runs`/`-max_total_time` is exhausted), and
reports a non-zero exit status if a crash, timeout, or sanitizer error is
found while processing an input.
