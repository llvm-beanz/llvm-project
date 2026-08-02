# FeMe Command Guide

This directory documents the command line tools that ship as part of FeMe
(see [../Design.md](../Design.md) for the overall design and roadmap). Each
tool has its own page:

- [`feme`](feme.md) — the primary, user-facing FeMe driver.
- [`feme-opt`](feme-opt.md) — testing tool for FeMe's MLIR passes/conversions
  in isolation.
- [`feme-translate`](feme-translate.md) — testing tool for FeMe's per-format
  import/export stages in isolation.
- [`feme-spirv-import-fuzzer`](feme-spirv-import-fuzzer.md) — libFuzzer
  harness for `feme::SPIRVImporter`.

`feme-opt` and `feme-translate` are testing-only entry points (see the "Core
Architectural Principle: No Global State" and "Testing Tools" sections of
[Design.md](../Design.md)): they exist to exercise individual pipeline
stages in isolation under `lit`/`FileCheck`, not as user-facing tools.
`feme` is the only tool intended for end users.
