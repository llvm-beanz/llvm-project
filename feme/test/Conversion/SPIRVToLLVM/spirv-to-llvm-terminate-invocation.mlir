// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.TerminateInvocation` (roadmap E12,
// VK_KHR_shader_terminate_invocation) converts to an unconditional
// discard-and-return: a call to the same `llvm.spv.discard` intrinsic
// `spirv.Kill` itself would use, followed by an `llvm.return`. Unlike
// `spirv.DemoteToHelperInvocation`, this op is a true terminator: no
// instruction of the invocation executes afterwards.

// CHECK-LABEL: llvm.func @terminate
// CHECK: llvm.call_intrinsic "llvm.spv.discard"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], [SPV_KHR_terminate_invocation]> {
  spirv.func @terminate() -> () "None" {
    spirv.TerminateInvocation
  }
}

// -----

// Checks the same conversion when `spirv.TerminateInvocation` is the sole
// terminator of a SPIR-V helper function whose *result type is not void*
// (e.g. `vec3 drawShape() { discard; return vec3(1.0); }`, roadmap L106's
// `dEQP-VK.graphicsfuzz.always-false-if-with-discard-return` case, which
// uses `OpTerminateInvocation` via `SPV_KHR_terminate_invocation`) -- the
// replacement `llvm.return` must carry a `poison` operand matching the
// enclosing (converted) `llvm.func`'s own non-void result type, or it
// fails `llvm.func`'s own verifier with "'llvm.return' op expected 1
// operand" (the confirmed failure signature this fixes).

// CHECK-LABEL: llvm.func @terminate_non_void() -> f32
// CHECK: llvm.call_intrinsic "llvm.spv.discard"() : () -> ()
// CHECK-NEXT: %[[POISON:.*]] = llvm.mlir.poison : f32
// CHECK-NEXT: llvm.return %[[POISON]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], [SPV_KHR_terminate_invocation]> {
  spirv.func @terminate_non_void() -> (f32) "None" {
    spirv.TerminateInvocation
  }
}
