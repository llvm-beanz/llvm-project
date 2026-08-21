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
