// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.CopyObject` (SPIR-V opcode 83, roadmap L86) -- a plain
// type-preserving copy with no dereferences -- erases entirely into its own
// already-converted operand, with no new IR emitted at all: LLVM IR's own
// SSA values already have this exact "another handle to the same value"
// semantics implicitly, so there is nothing left for a real copy to do once
// lowered this far.

// CHECK-LABEL: llvm.func @copy_object_scalar(%arg0: f32) -> f32
// CHECK-NEXT: llvm.return %arg0 : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @copy_object_scalar(%arg0 : f32) -> f32 "None" {
    %0 = spirv.CopyObject %arg0 : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// Same, for a vector-typed operand -- confirms the erasure isn't scalar-only.

// CHECK-LABEL: llvm.func @copy_object_vector(%arg0: vector<4xi32>) -> vector<4xi32>
// CHECK-NEXT: llvm.return %arg0 : vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @copy_object_vector(%arg0 : vector<4xi32>) -> vector<4xi32> "None" {
    %0 = spirv.CopyObject %arg0 : vector<4xi32>
    spirv.ReturnValue %0 : vector<4xi32>
  }
}
