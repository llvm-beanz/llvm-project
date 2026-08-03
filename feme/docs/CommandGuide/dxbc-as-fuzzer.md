# `dxbc-as-fuzzer` — fuzzer for the DXBC assembly parser

## SYNOPSIS

```shell
dxbc-as-fuzzer [libFuzzer options] [corpus dir...]
```

## DESCRIPTION

`dxbc-as-fuzzer` is a [libFuzzer](../../../llvm/docs/LibFuzzer.md) harness
for `feme::dxbc::parseAssembly` (and, on every input the parser accepts,
`feme::dxbc::encodeProgram`/`wrapInContainer` — see
[dxbc-as.md](dxbc-as.md)). Unlike `feme-dxil-import-fuzzer` and
`feme-spirv-import-fuzzer`, which fuzz *binary*-format importers,
`dxbc-as`'s input format is itself text: `dxbc-as` exists specifically to
make it easy to hand-author (and, by extension, fuzz) DXBC test inputs, so
its own parser must be just as robust against malformed/adversarial input
as any binary-format parser in the tree. See the "Testing Strategy" section
of [../Design.md](../Design.md) for why every FeMe-adjacent parser ships a
fuzzer.

Each fuzzer iteration feeds the fuzzer-provided bytes to `parseAssembly` as
raw text (which may or may not be syntactically valid DXBC assembly); on
success, it additionally re-prints the parsed program
(`printAssembly`) and encodes it for every supported `ShaderKind`
(`encodeProgram`/`wrapInContainer`), discarding any resulting `Error` — the
harness only needs every stage to never crash, hang, or otherwise misbehave,
not to check any particular output.

This tool is a testing-only entry point; it is not intended for end users
and has no CLI options of its own — all options come from libFuzzer itself.

## BUILDING

Like `feme-dxil-import-fuzzer` (see
[feme-dxil-import-fuzzer.md](feme-dxil-import-fuzzer.md)'s BUILDING
section, which applies here unchanged), `-DLLVM_ENABLE_PROJECTS=feme` alone
builds a **dummy** binary (`DummyFuzzer.cpp`) that only runs
`LLVMFuzzerTestOneInput` once per file argument. Configure the build with
`-DLLVM_USE_SANITIZER=Address -DLLVM_USE_SANITIZE_COVERAGE=On` (or link
against a standalone-built libFuzzer via `LLVM_LIB_FUZZING_ENGINE`) to get a
real, continuously-mutating libFuzzer binary.

## OPTIONS

`dxbc-as-fuzzer` accepts the standard set of
[libFuzzer options](../../../llvm/docs/LibFuzzer.md#options) (e.g. `-runs=N`,
`-max_len=N`, `-jobs=N`), plus one or more corpus directory arguments. Run
`dxbc-as-fuzzer -help=1` for the full, current list of libFuzzer options.

## EXAMPLES

Run the fuzzer against a corpus directory, seeding it from the small,
checked-in seed corpus (see
[../../tools/dxbc-as-fuzzer/seed-corpus](../../tools/dxbc-as-fuzzer/seed-corpus))
so the fuzzer starts from valid DXBC assembly text instead of an empty
corpus:

```shell
mkdir -p corpus
cp <feme-source-dir>/tools/dxbc-as-fuzzer/seed-corpus/*.dxasm corpus/
dxbc-as-fuzzer corpus
```

Run a fixed number of iterations, useful for quick regression checks in CI:

```shell
dxbc-as-fuzzer -runs=100000 corpus
```

## EXIT STATUS

`dxbc-as-fuzzer` follows standard libFuzzer conventions: it runs until
stopped (or until `-runs`/`-max_total_time` is exhausted), and reports a
non-zero exit status if a crash, timeout, or sanitizer error is found while
processing an input.
