# `feme-vulkan-pipeline-cache-fuzzer` — fuzzer for the `VkPipelineCache` blob parser

## SYNOPSIS

```shell
feme-vulkan-pipeline-cache-fuzzer [libFuzzer options] [corpus dir...]
```

## DESCRIPTION

`feme-vulkan-pipeline-cache-fuzzer` is a [libFuzzer](../../../llvm/docs/LibFuzzer.md)
harness for `feme::vulkan::parsePipelineCacheBlob`
(`feme/lib/Vulkan/PipelineCache.cpp`), the parser behind
`vkCreatePipelineCache`. That blob is fully attacker-controlled input --
typically loaded from a file the application wrote earlier and may have been
tampered with (see "Pipeline Cache" in
[FeMeVulkanDesign.md](../FeMeVulkanDesign.md)) -- so fuzzing it is a V4
requirement, matching how every other externally-defined binary format FeMe
consumes (SPIR-V, DXIL, DXBC) is already fuzzed.

Each iteration calls `parsePipelineCacheBlob` directly with the
fuzzer-provided bytes against a fixed, arbitrary UUID/vendor/device triple,
discarding the result either way -- the harness only needs the parser to
never crash, hang, read out of bounds, or otherwise misbehave on malformed
input (including a blob whose header matches but whose digest, key count, or
trailing data does not), not to check any particular output. The parser
itself has no global state to reset between iterations.

This tool is a testing-only entry point; it is not intended for end users and
has no CLI options of its own — all options come from libFuzzer itself. It is
only built when a Vulkan SDK is found (`FEME_ENABLE_VULKAN`, the default).

## BUILDING

Like `feme-dxil-import-fuzzer` (see
[feme-dxil-import-fuzzer.md](feme-dxil-import-fuzzer.md)'s BUILDING
section, which applies here unchanged), `-DLLVM_ENABLE_PROJECTS=feme` alone
builds a **dummy** binary (`DummyPipelineCacheFuzzer.cpp`) that only runs
`LLVMFuzzerTestOneInput` once per file argument. Configure the build with
`-DLLVM_USE_SANITIZER=Address -DLLVM_USE_SANITIZE_COVERAGE=On` (or link
against a standalone-built libFuzzer via `LLVM_LIB_FUZZING_ENGINE`) to get a
real, continuously-mutating libFuzzer binary.

## OPTIONS

`feme-vulkan-pipeline-cache-fuzzer` accepts the standard set of
[libFuzzer options](../../../llvm/docs/LibFuzzer.md#options) (e.g. `-runs=N`,
`-max_len=N`, `-jobs=N`), plus one or more corpus directory arguments. Run
`feme-vulkan-pipeline-cache-fuzzer -help=1` for the full, current list of
libFuzzer options.

## EXAMPLES

Run the fuzzer against a corpus directory, seeding it from the small,
checked-in seed corpus (see
[../../tools/feme-vulkan-pipeline-cache-fuzzer/seed-corpus](../../tools/feme-vulkan-pipeline-cache-fuzzer/seed-corpus)
-- valid blobs over the fuzzer's own fixed UUID/vendor/device, produced by
`serializePipelineCacheBlob`) so the fuzzer starts from well-formed blobs
instead of an empty corpus:

```shell
mkdir -p corpus
cp <feme-source-dir>/tools/feme-vulkan-pipeline-cache-fuzzer/seed-corpus/*.bin corpus/
feme-vulkan-pipeline-cache-fuzzer corpus
```

Run a fixed number of iterations, useful for quick regression checks in CI
(`ninja check-feme-fuzz` does exactly this, over this corpus, for every
fuzzer in the tree):

```shell
feme-vulkan-pipeline-cache-fuzzer -runs=100000 corpus
```

Reproduce a specific crashing input found by a previous fuzzing run:

```shell
feme-vulkan-pipeline-cache-fuzzer crash-<hash>
```

## EXIT STATUS

`feme-vulkan-pipeline-cache-fuzzer` follows standard libFuzzer conventions:
it runs until stopped (or until `-runs`/`-max_total_time` is exhausted), and
reports a non-zero exit status if a crash, timeout, or sanitizer error is
found while processing an input.
