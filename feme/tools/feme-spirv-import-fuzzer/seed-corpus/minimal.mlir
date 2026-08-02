// Source text for seed-corpus/minimal.spv (see ../seed-corpus in
// feme-spirv-import-fuzzer.md for regeneration instructions). Kept as a
// human-readable, diffable companion to the checked-in binary, matching how
// SPIRVImporterTest.cpp builds its minimal module.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}
