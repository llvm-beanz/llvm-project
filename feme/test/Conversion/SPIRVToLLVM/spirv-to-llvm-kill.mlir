// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.Kill` (roadmap H99, `OpKill` -- the common lowering
// target of HLSL's unconditional `discard`) converts to an unconditional
// discard-and-return: a call to the `llvm.spv.discard` intrinsic, followed
// by an `llvm.return`. MLIR's upstream SPIRVToLLVM conversion has no
// pattern for this op at all, so it previously failed legalization with
// "failed to legalize operation 'spirv.Kill' that was explicitly marked
// illegal" whenever a shader reached it.

// CHECK-LABEL: llvm.func @kill
// CHECK: llvm.call_intrinsic "llvm.spv.discard"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @kill() -> () "None" {
    spirv.Kill
  }
}
