# FeMe Command Guide

This directory documents the command line tools that ship as part of FeMe
(see [../Design.md](../Design.md) for the overall design and roadmap). Each
tool has its own page:

- [`feme`](feme.md) — the primary, user-facing FeMe driver.
- [`feme-opt`](feme-opt.md) — testing tool for FeMe's MLIR passes/conversions
  in isolation.
- [`feme-run`](feme-run.md) — testing tool that JITs and dispatches a raised
  shader against a textual resource heap, for the FeMe CPU target.
- [`feme-translate`](feme-translate.md) — testing tool for FeMe's per-format
  import/export stages in isolation.
- [`dxbc-as`](dxbc-as.md) — standalone DXBC assembler, used to build
  human-readable DXBC test fixtures.
- [`feme-spirv-import-fuzzer`](feme-spirv-import-fuzzer.md) — libFuzzer
  harness for `feme::SPIRVImporter`.
- [`feme-dxil-import-fuzzer`](feme-dxil-import-fuzzer.md) — libFuzzer
  harness for `feme::DXILImporter`.
- [`feme-dxbc-import-fuzzer`](feme-dxbc-import-fuzzer.md) — libFuzzer
  harness for `feme::dxsa::deserialize` (the DXBC importer's `BinaryParser`).
- [`dxbc-as-fuzzer`](dxbc-as-fuzzer.md) — libFuzzer harness for `dxbc-as`'s
  own assembly parser.
- [`feme-cfg-gen`](feme-cfg-gen.md) — seeded generator for CFG-shaped
  shaders, for the FeMe CPU target's restructurization test suite.
- [`feme-cpu-restructure-fuzzer`](feme-cpu-restructure-fuzzer.md) —
  libFuzzer harness for CFG restructurization (`FixIrreducible` +
  `StructurizeCFG`), over `feme-cfg-gen` seeds.

`feme-opt`, `feme-run`, `feme-translate`, and `dxbc-as` are testing-only
entry points
(see the "Core Architectural Principle: No Global State" and "Testing
Tools" sections of [Design.md](../Design.md)): they exist to exercise
individual pipeline stages in isolation under `lit`/`FileCheck`, not as
user-facing tools. `feme` is the only tool intended for end users.
