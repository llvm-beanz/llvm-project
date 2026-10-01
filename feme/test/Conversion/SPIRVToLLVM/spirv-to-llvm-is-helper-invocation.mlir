// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.IsHelperInvocationEXT` (roadmap L291,
// SPV_EXT_demote_to_helper_invocation's read-only query counterpart to
// `spirv.DemoteToHelperInvocation`) converts to a call to the
// `llvm.spv.is.helper.invocation` intrinsic, with the op's result forwarded
// to the intrinsic call's result.

// CHECK-LABEL: llvm.func @is_helper
// CHECK: %[[RES:.*]] = llvm.call_intrinsic "llvm.spv.is.helper.invocation"() : () -> i1
// CHECK: llvm.return %[[RES]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage, DemoteToHelperInvocation], [SPV_EXT_demote_to_helper_invocation]> {
  spirv.func @is_helper() -> (i1) "None" {
    %0 = spirv.IsHelperInvocationEXT : i1
    spirv.ReturnValue %0 : i1
  }
}
