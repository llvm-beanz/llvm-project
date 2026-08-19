// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.Switch` converts to `llvm.switch`, which MLIR has no
// pattern for at all (see the "`spirv.Switch` op is not supported at the
// moment" note in `mlir::populateSPIRVToLLVMConversionPatterns`'s
// structured-loop pattern).

// A switch with real cases: the selector's signed `si32` type has to convert
// to the case literals' (now signless) type too, or the converted op's
// verifier rejects the mismatch.

// CHECK-LABEL: llvm.func @cases
// CHECK: llvm.switch %arg0 : i32, ^[[DEFAULT:.*]] [
// CHECK-NEXT: 0: ^[[CASE0:.*]],
// CHECK-NEXT: 1: ^[[CASE1:.*]]
// CHECK-NEXT: ]
// CHECK: ^[[CASE0]]:
// CHECK: llvm.return %arg1
// CHECK: ^[[CASE1]]:
// CHECK: llvm.return %arg2
// CHECK: ^[[DEFAULT]]:
// CHECK: llvm.return %arg3
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @cases(%selector : si32, %a : f32, %b : f32, %c : f32) -> f32 "None" {
    spirv.Switch %selector : si32, [
      default: ^default,
      0: ^case0,
      1: ^case1
    ]
  ^case0:
    spirv.ReturnValue %a : f32
  ^case1:
    spirv.ReturnValue %b : f32
  ^default:
    spirv.ReturnValue %c : f32
  }
}

// -----

// A case-less switch (only a default successor) is what `spirv-opt`'s
// merge-return pass emits to skip the rest of a function after an early
// return, and converts to an unconditional `llvm.switch` with no cases.

// CHECK-LABEL: llvm.func @no_cases
// CHECK: llvm.switch %arg0 : i32, ^[[DEFAULT:.*]] [
// CHECK-NEXT: ]
// CHECK: ^[[DEFAULT]]:
// CHECK: llvm.return %arg1
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @no_cases(%selector : i32, %a : f32) -> f32 "None" {
    spirv.Switch %selector : i32, [
      default: ^default
    ]
  ^default:
    spirv.ReturnValue %a : f32
  }
}
