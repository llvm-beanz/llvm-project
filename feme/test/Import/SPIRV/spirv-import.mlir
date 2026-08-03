// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | FileCheck %s

// Round-trips a minimal SPIR-V module through feme::SPIRVImporter: this
// file's `spirv` dialect text is serialized to a real SPIR-V binary with
// MLIR's own (generically-registered) `--serialize-spirv`, then that binary
// is fed through feme-translate's `--import-spirv` (feme::SPIRVImporter,
// see feme/docs/Design.md's "SPIR-V import" roadmap step) and the resulting
// `spirv` dialect text is checked below. No binary fixture is checked in,
// per "Avoiding binary test fixtures" in feme/docs/Design.md.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// CHECK:      spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
// CHECK-NEXT:   spirv.func @foo() "Inline" {
// CHECK-NEXT:     spirv.Return
// CHECK-NEXT:   }
// CHECK-NEXT:   spirv.EntryPoint "Vertex" @foo
// CHECK-NEXT: }
