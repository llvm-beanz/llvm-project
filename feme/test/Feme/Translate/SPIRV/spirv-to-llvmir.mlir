// RUN: feme-translate --no-implicit-module --spirv-to-llvmir %s | FileCheck %s

// Runs feme::SPIRVToLLVMTranslator (via feme-translate's `--spirv-to-llvmir`,
// see feme/docs/Design.md's "Testing Tools" section) on a minimal hand-written
// `spirv` dialect module, and checks that the resulting LLVM IR defines the
// original entry point as a real (non-declaration) function.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @foo() -> () "Inline" {
    spirv.Return
  }
  spirv.EntryPoint "Vertex" @foo
}

// CHECK: define void @foo()
// CHECK-NEXT: ret void
// CHECK-NEXT: }
