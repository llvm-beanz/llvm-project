# `feme-dxbc-import-fuzzer` — fuzzer for the DXBC importer's `BinaryParser`

## SYNOPSIS

```shell
feme-dxbc-import-fuzzer [libFuzzer options] [corpus dir...]
```

## DESCRIPTION

`feme-dxbc-import-fuzzer` is a [libFuzzer](../../../llvm/docs/LibFuzzer.md)
harness for `feme::dxsa::deserialize` (`feme/lib/Target/DXSA/BinaryParser.cpp`),
the DXBC importer's hand-written token decoder. FeMe consumes
externally-defined binary formats (SPIR-V, DXIL, DXBC) supplied by untrusted
sources at driver runtime, so fuzzing each `Importer` is a v1 requirement
(see the "Testing Strategy" section of [../Design.md](../Design.md)),
matching how other LLVM binary-format parsers (e.g. `llvm-dis-fuzzer` for
bitcode) are fuzzed. `BinaryParser.cpp` is ~3800 lines of hand-written
decoding over untrusted input, and, unlike [`dxbc-as-fuzzer`](dxbc-as-fuzzer.md)
(which fuzzes the assembler's *text* parser), this harness exercises the
same binary token stream a real DXBC-carrying driver input would.

Each fuzzer iteration constructs a fresh `mlir::MLIRContext` (with the
`dxsa` dialect registered, matching
`feme::registerDXSAImportBinTranslation`, `--import-dxsa-bin`'s
non-fuzzer entry point to the same `deserialize` call) and feeds the
fuzzer-provided bytes to it as a raw token buffer (which may or may not
parse as a well-formed DXBC instruction stream), discarding the resulting
`ModuleOp` (or null, on a parse failure) either way — the harness only
needs the parser to never crash, hang, or otherwise misbehave on malformed
input, not to check any particular output. A fresh `MLIRContext` per input
matches how the SPIR-V/DXIL importer fuzzers use a fresh `feme::Context`
per input: `BinaryParser` must not rely on any state surviving across
calls (see the "No Global State" principle in [../Design.md](../Design.md)).

This tool is a testing-only entry point; it is not intended for end users
and has no CLI options of its own — all options come from libFuzzer itself.

## BUILDING

Like `feme-dxil-import-fuzzer` (see
[feme-dxil-import-fuzzer.md](feme-dxil-import-fuzzer.md)'s BUILDING
section, which applies here unchanged), `-DLLVM_ENABLE_PROJECTS=feme`
alone builds a **dummy** binary (`DummyImporterFuzzer.cpp`) that only runs
`LLVMFuzzerTestOneInput` once per file argument. Configure the build with
`-DLLVM_USE_SANITIZER=Address -DLLVM_USE_SANITIZE_COVERAGE=On` (or link
against a standalone-built libFuzzer via `LLVM_LIB_FUZZING_ENGINE`) to get
a real, continuously-mutating libFuzzer binary.

## OPTIONS

`feme-dxbc-import-fuzzer` accepts the standard set of
[libFuzzer options](../../../llvm/docs/LibFuzzer.md#options) (e.g.
`-runs=N`, `-max_len=N`, `-jobs=N`), plus one or more corpus directory
arguments. Run `feme-dxbc-import-fuzzer -help=1` for the full, current
list of libFuzzer options.

## EXAMPLES

Run the fuzzer against a corpus directory, seeding it from the small,
checked-in seed corpus (see
[../../tools/feme-dxbc-import-fuzzer/seed-corpus](../../tools/feme-dxbc-import-fuzzer/seed-corpus))
so the fuzzer starts from valid DXBC token streams instead of an empty
corpus:

```shell
mkdir -p corpus
cp <feme-source-dir>/tools/feme-dxbc-import-fuzzer/seed-corpus/*.dxbc corpus/
feme-dxbc-import-fuzzer corpus
```

Run a fixed number of iterations, useful for quick regression checks in CI
(`ninja check-feme-fuzz` does exactly this, over this corpus, for every
fuzzer in the tree):

```shell
feme-dxbc-import-fuzzer -runs=100000 corpus
```

Reproduce a specific crashing input found by a previous fuzzing run:

```shell
feme-dxbc-import-fuzzer crash-<hash>
```

## EXIT STATUS

`feme-dxbc-import-fuzzer` follows standard libFuzzer conventions: it runs
until stopped (or until `-runs`/`-max_total_time` is exhausted), and
reports a non-zero exit status if a crash, timeout, or sanitizer error is
found while processing an input.
