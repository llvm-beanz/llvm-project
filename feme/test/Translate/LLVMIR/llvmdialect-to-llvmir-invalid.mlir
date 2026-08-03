// RUN: not feme-translate --llvmdialect-to-llvmir %s 2>&1 | FileCheck %s

// feme::LLVMDialectToLLVMIRTranslator (via feme-translate's
// `--llvmdialect-to-llvmir`) only registers the `llvm` dialect for parsing;
// check that a module using another dialect's ops (here `arith`, which
// hasn't itself been lowered to the `llvm` dialect) is rejected with a
// clear diagnostic rather than silently mistranslated.

module {
  llvm.func @foo() {
    %0 = arith.constant 0 : i32
    llvm.return
  }
}

// CHECK: Dialect `arith' not found for custom op 'arith.constant'
