# `feme-dxil-import-fuzzer` — fuzzer for `feme::DXILImporter`

## SYNOPSIS

```shell
feme-dxil-import-fuzzer [libFuzzer options] [corpus dir...]
```

## DESCRIPTION

`feme-dxil-import-fuzzer` is a [libFuzzer](../../../llvm/docs/LibFuzzer.md)
harness for `feme::DXILImporter`. FeMe consumes externally-defined binary
formats (SPIR-V, DXIL, DXBC) supplied by untrusted sources at driver
runtime, so fuzzing each `Importer` is a v1 requirement (see the "Testing
Strategy" section of [../Design.md](../Design.md)), matching how other LLVM
binary-format parsers (e.g. `llvm-dis-fuzzer` for bitcode) are fuzzed.
`DXILImporter` in particular runs both a `DXContainer` parser and LLVM's
bitcode reader over untrusted input (see the "DXIL import" section of
[../Design.md](../Design.md)), so it exercises more attack surface than a
typical importer.

Each fuzzer iteration constructs a fresh `feme::Context` and
`feme::DXILImporter`, feeds the fuzzer-provided bytes to
`DXILImporter::import` as a raw buffer (which may or may not parse as
either a `DXContainer` or LLVM bitcode), and discards any resulting
`Error` — the harness only needs the importer to never crash, hang, or
otherwise misbehave on malformed input, not to check any particular
output. A fresh `Context` per input matches how other in-tree fuzzers
(e.g. `llvm-dis-fuzzer`) use a fresh `LLVMContext` per input: Importers
must not rely on any state surviving across calls (see the "No Global
State" principle in [../Design.md](../Design.md)).

This tool is a testing-only entry point; it is not intended for end users
and has no CLI options of its own — all options come from libFuzzer itself.

## BUILDING

`feme-dxil-import-fuzzer` is built with LLVM's `add_llvm_fuzzer` CMake
helper (see [LibFuzzer](../../../llvm/docs/LibFuzzer.md) and
[FuzzingLLVM](../../../llvm/docs/FuzzingLLVM.rst)), which is *not*
automatically linked against a real libFuzzer: by default,
`-DLLVM_ENABLE_PROJECTS=feme` alone builds a **dummy** binary
(`DummyImporterFuzzer.cpp`) that only runs `LLVMFuzzerTestOneInput` once per
file argument — no continuous mutation, no coverage feedback, no crash
minimization. Running it against a corpus *directory* (as opposed to
individual files) will just fail with `Error reading file: <dir>: Is a
directory`, since the dummy driver cannot open a directory as an input
file. If you see this, or the banner below, on startup, you have the dummy
build, not a real fuzzer:

```
*** This tool was not linked to libFuzzer.
*** No fuzzing will be performed.
```

To get a real, continuously-mutating libFuzzer binary, configure the build
with sanitizer coverage instrumentation, e.g.:

```shell
cmake -S llvm -B build -G Ninja \
  -DLLVM_ENABLE_PROJECTS=feme \
  -DLLVM_USE_SANITIZER=Address \
  -DLLVM_USE_SANITIZE_COVERAGE=On \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build feme-dxil-import-fuzzer
```

This requires a `clang` whose resource directory ships the `fuzzer` runtime
(`libclang_rt.fuzzer*.a`); most Linux/macOS clang installs targeting
`x86_64`/`aarch64` include it, but some minimal or cross-compiled toolchains
do not. If your toolchain lacks it, build libFuzzer standalone from the
in-tree sources and point `LLVM_LIB_FUZZING_ENGINE` at the resulting
archive instead — this still uses the real libFuzzer driver loop, just
linked explicitly rather than via `-fsanitize=fuzzer`:

```shell
CXX=clang++ sh compiler-rt/lib/fuzzer/build.sh   # produces ./libFuzzer.a
cmake -S llvm -B build -G Ninja \
  -DLLVM_ENABLE_PROJECTS=feme \
  -DLLVM_LIB_FUZZING_ENGINE=$PWD/libFuzzer.a \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build feme-dxil-import-fuzzer
```

Note that without `LLVM_USE_SANITIZE_COVERAGE` (or another source of
`-fsanitize=fuzzer-no-link` instrumentation on FeMe's own libraries), this
second form still exercises `DXILImporter` on every input and reports
crashes/hangs, but mutations are not guided by code coverage feedback, so
it is best treated as a way to smoke-test the harness rather than to run a
long, effective fuzzing campaign.

Either way, confirm you have a real fuzzer (not the dummy) by checking
`-help=1`: a real libFuzzer binary prints the full set of libFuzzer flags
(`-runs`, `-max_len`, `-jobs`, ...), while the dummy binary accepts no
flags of its own.

## OPTIONS

`feme-dxil-import-fuzzer` accepts the standard set of
[libFuzzer options](../../../llvm/docs/LibFuzzer.md#options) (e.g. `-runs=N`,
`-max_len=N`, `-jobs=N`), plus one or more corpus directory arguments. Run
`feme-dxil-import-fuzzer -help=1` for the full, current list of libFuzzer
options.

## EXAMPLES

Run the fuzzer against a corpus directory, seeding it from the small,
checked-in seed corpus (see
[../../tools/feme-dxil-import-fuzzer/seed-corpus](../../tools/feme-dxil-import-fuzzer/seed-corpus))
so the fuzzer starts from valid DXIL inputs instead of an empty corpus:

```shell
mkdir -p corpus
cp <feme-source-dir>/tools/feme-dxil-import-fuzzer/seed-corpus/*.bc corpus/
cp <feme-source-dir>/tools/feme-dxil-import-fuzzer/seed-corpus/*.dxcontainer corpus/
feme-dxil-import-fuzzer corpus
```

Run a fixed number of iterations, useful for quick regression checks in CI:

```shell
feme-dxil-import-fuzzer -runs=100000 corpus
```

Reproduce a specific crashing input found by a previous fuzzing run:

```shell
feme-dxil-import-fuzzer crash-<hash>
```

## EXIT STATUS

`feme-dxil-import-fuzzer` follows standard libFuzzer conventions: it runs
until stopped (or until `-runs`/`-max_total_time` is exhausted), and
reports a non-zero exit status if a crash, timeout, or sanitizer error is
found while processing an input.
