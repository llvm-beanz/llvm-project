// RUN: feme-translate --no-implicit-module --llvmdialect-to-llvmir %s | FileCheck %s

// Runs feme::LLVMDialectToLLVMIRTranslator (via feme-translate's
// `--llvmdialect-to-llvmir`, see feme/docs/Design.md's "SPIR-V -> MLIR llvm
// dialect -> LLVM IR" section) on a minimal hand-written `llvm` dialect
// module, and checks that the resulting LLVM IR defines the original
// function as a real (non-declaration) function. Unlike
// test/Translate/SPIRV/spirv-to-llvmdialect.mlir, this Translator is not
// SPIR-V-specific -- it accepts any `llvm` dialect module, which this test
// exercises directly rather than via a `spirv` -> `llvm` conversion.

module {
  llvm.func @foo() {
    llvm.return
  }
}

// CHECK: define void @foo()
// CHECK-NEXT: ret void
// CHECK-NEXT: }
