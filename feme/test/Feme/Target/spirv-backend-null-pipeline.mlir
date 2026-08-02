// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o %t.ll
// RUN: feme-translate --llvm-backend --target-triple=spirv64-unknown-unknown \
// RUN:   %t.ll -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s
//
// REQUIRES: spirv-registered-target

// Exercises the SPIR-V retargeting "null pipeline" described in the
// deviation note in feme/docs/Design.md end to end:
//
//   SPIR-V binary -> SPIRVImporter -> spirv dialect
//                  -> SPIRVToLLVMTranslator -> llvm::Module
//                  -> TargetMachineBackend(spirv64) -> SPIR-V binary
//                  -> SPIRVImporter -> spirv dialect (round-tripped)
//
// validating the Translator/Backend plumbing by retargeting back to the
// format that was imported, rather than a real ISA. Each stage above is
// exercised through feme-translate (see feme/docs/Design.md's "Testing
// Tools" section) instead of only via gtest: `--serialize-spirv` (MLIR's
// own, generically-registered translation) and `--import-spirv` build and
// re-import the SPIR-V binary; `--spirv-to-llvmir` and `--llvm-backend`
// wrap feme::SPIRVToLLVMTranslator and feme::TargetMachineBackend
// respectively.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// The re-emitted binary is re-run through the same feme::SPIRVImporter
// used above: if the null pipeline round-tripped correctly, this recovers
// the original entry point as a real (non-declaration) spirv.func. The
// surrounding module header (addressing/memory model, capabilities) is
// *not* preserved identically: LLVM's SPIRV target derives those from the
// llvm::Module it was given, independent of the original SPIR-V module's
// execution environment (Shader vs Kernel) -- expected here since this
// null pipeline validates the Translator/Backend plumbing, not exact
// module-header fidelity (see feme/docs/Design.md's deviation note).
// CHECK: spirv.func @foo()
// CHECK-NEXT: spirv.Return
// CHECK-NEXT: }
