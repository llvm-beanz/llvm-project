// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.DemoteToHelperInvocation` (roadmap E11,
// VK_EXT_shader_demote_to_helper_invocation) converts to a call to the
// `llvm.spv.demote.to.helper.invocation` intrinsic. Unlike `spirv.Kill`
// (which converts to the terminating `llvm.spv.discard`), this op is not a
// terminator: execution continues afterwards.

// CHECK-LABEL: llvm.func @demote
// CHECK: llvm.call_intrinsic "llvm.spv.demote.to.helper.invocation"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage, DemoteToHelperInvocation], [SPV_EXT_demote_to_helper_invocation]> {
  spirv.func @demote() -> () "None" {
    spirv.DemoteToHelperInvocation
    spirv.Return
  }
}
