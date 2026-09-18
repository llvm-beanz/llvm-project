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

// -----

// Checks the same conversion when `spirv.Kill` is the sole terminator of a
// SPIR-V helper function whose *result type is not void* (e.g. `vec3
// drawShape() { discard; return vec3(1.0); }`, roadmap L106's
// `dEQP-VK.graphicsfuzz.always-false-if-with-discard-return` case) --
// unlike the void-returning case above, the enclosing (converted)
// `llvm.func`'s own non-void result type means the replacement
// `llvm.return` must carry a `poison` operand of that type, or it fails
// `llvm.func`'s own verifier with "'llvm.return' op expected 1 operand"
// (the confirmed failure signature this fixes).

// CHECK-LABEL: llvm.func @kill_non_void() -> vector<3xf32>
// CHECK: llvm.call_intrinsic "llvm.spv.discard"() : () -> ()
// CHECK-NEXT: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK-NEXT: llvm.return %[[POISON]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @kill_non_void() -> (vector<3xf32>) "None" {
    spirv.Kill
  }
}
