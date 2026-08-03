// RUN: feme-translate --no-implicit-module --spirv-to-llvmdialect %s | FileCheck %s

// Runs feme::SPIRVToLLVMDialectTranslator (via feme-translate's
// `--spirv-to-llvmdialect`, see feme/docs/Design.md's "SPIR-V -> MLIR llvm
// dialect -> LLVM IR" section) on a minimal hand-written `spirv` dialect
// module, and checks that the resulting module is in the `llvm` dialect
// (not yet translated all the way to LLVM IR), carries the target triple and
// data layout of the SPIR-V environment the module was compiled for, and has
// the original entry point preserved as a real (non-declaration) `llvm.func`.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// CHECK: module attributes
// CHECK-SAME: llvm.data_layout = "e-ve-i64:64-n8:16:32:64-G10"
// CHECK-SAME: llvm.target_triple = "spirv-unknown-vulkan-vertex"
// CHECK: llvm.func @foo()
// CHECK-NEXT: llvm.return
// CHECK-NEXT: }
