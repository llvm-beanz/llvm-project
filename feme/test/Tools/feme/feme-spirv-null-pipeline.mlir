// REQUIRES: spirv-registered-target
// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme --from=spirv --to=spirv %t.spv -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s

// Exercises the SPIR-V "null pipeline" (see the deviation note in
// feme/docs/Design.md's Retargeting to Native ISA section) through the full
// `feme` CLI/`feme::Driver`, rather than composing it one
// `feme-translate` stage at a time as test/Target/spirv-backend-null-pipeline.mlir
// does: `feme --from=spirv --to=spirv` should import this module, translate
// it to `llvm::Module` (feme::SPIRVToLLVMTranslator), and retarget it back
// to a SPIR-V binary (feme::TargetMachineBackend targeting
// "spirv64-unknown-unknown", feme::Driver's default for `--to=spirv`) in one
// invocation. Re-importing the result recovers the original entry point,
// the same way the `feme-translate`-composed pipeline does.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// CHECK: spirv.func @foo()
// CHECK-NEXT: spirv.Return
// CHECK-NEXT: }
