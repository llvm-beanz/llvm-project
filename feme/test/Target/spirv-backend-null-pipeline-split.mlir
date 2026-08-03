// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmdialect - -o %t.llvmdialect.mlir
// RUN: feme-translate --no-implicit-module --llvmdialect-to-llvmir \
// RUN:   %t.llvmdialect.mlir -o %t.ll
// RUN: feme-translate --llvm-backend --target-triple=spirv64-unknown-unknown \
// RUN:   %t.ll -o %t.roundtrip.spv
// RUN: feme-translate --import-spirv %t.roundtrip.spv | FileCheck %s
//
// REQUIRES: spirv-registered-target

// Exercises the same SPIR-V retargeting "null pipeline" as
// spirv-backend-null-pipeline.mlir, but going through the individual
// `spirv` -> `llvm` dialect -> LLVM IR stages
// (feme::SPIRVToLLVMDialectTranslator, feme::LLVMDialectToLLVMIRTranslator)
// rather than the combined feme::SPIRVToLLVMTranslator:
//
//   SPIR-V binary -> SPIRVImporter -> spirv dialect
//                  -> SPIRVToLLVMDialectTranslator -> llvm dialect
//                  -> LLVMDialectToLLVMIRTranslator -> llvm::Module
//                  -> TargetMachineBackend(spirv64) -> SPIR-V binary
//                  -> SPIRVImporter -> spirv dialect (round-tripped)
//
// This is the literal three-stage "read SPIR-V into MLIR, translate to the
// `llvm` dialect, then to LLVM IR" flow described in the "SPIR-V -> MLIR
// llvm dialect -> LLVM IR" section of feme/docs/Design.md, checked here to
// produce an identical round-trip result to the combined
// `--spirv-to-llvmir` pipeline.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// See spirv-backend-null-pipeline.mlir for why only the entry point (not
// the full module header) is checked.
// CHECK: spirv.func @foo()
// CHECK-NEXT: spirv.Return
// CHECK-NEXT: }
