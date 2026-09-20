// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(u) regression test: matches `dEQP-VK.ssbo.layout.random.
// nested_structs_instance_arrays.8`'s own `sD.mA` (`!sB` below) shape --
// a nested struct (`!sB`) embedded two levels deep (`Block` -> `!sD` ->
// `!sB`) whose own body is `vector<2xf32>, vector<3xf32>, mat3x2
// (RowMajor), f32`.
//
// Root cause: `getStructMemberPhysicalIndex` re-derives a nested struct's
// physical layout by converting it *standalone*, as if it were top-level.
// For `!sB` here, that standalone conversion trivially succeeds at the
// "natural" (tier 1) retry: a bare `vector<3xf32>` member's generic LLVM
// size (16 bytes, rounded) happens to exactly reach the next member's
// declared offset, so no interior gap is inserted.
//
// But `!sB` embedded for real inside `!sD` uses a *different* physical
// layout: `!sD`'s own natural (tier 1) conversion fails (its `si32`
// member right after `!sB` can't be reached without a gap), forcing
// `!sD`'s own conversion to retry at the `VectorOnly` tier. That tier's
// per-member substitution loop unconditionally calls
// `getTightNestedStructType` on any nested-struct member -- which always
// force-tightens every interior vector/matrix member of that nested
// struct, regardless of whether the nested struct itself would need
// tightening in isolation. This inserts an interior gap before `!sB`'s
// `mat3x2` member that the standalone (tier 1) conversion never
// produces, giving `!sB`-embedded a different (correct) physical layout
// than `!sB`-converted-alone.
//
// This means whether a nested struct's own body gets tightened is a
// property of which retry tier its *enclosing* struct actually needed --
// not an intrinsic property of the nested struct type itself. Fixed by
// `getStructMemberPhysicalIndexInRealType`, which reads the declared-to-
// physical index mapping directly off the already-known-correct real
// LLVM type used by the immediate enclosing struct's own conversion,
// rather than re-deriving the nested struct's layout independently.
//
// Without the fix, the physical index for `!sD`'s `f32` member here
// (declared index 3, indexed via a two-level AccessChain through `!sB`)
// is computed as 4 (from `!sB`'s standalone, untightened layout) instead
// of the correct 5 (from `!sB`'s real, tightened-with-interior-gap
// layout as embedded in `!sD`).

!sB = !spirv.struct<(vector<2xf32> [0], vector<3xf32> [16], !spirv.matrix<3 x vector<2xf32>> [32, RowMajor, MatrixStride=16], f32 [64])>
!sD = !spirv.struct<(!sB [0], si32 [80], vector<3xf32> [96])>

// CHECK-LABEL: llvm.func @read
// CHECK: llvm.getelementptr inbounds %{{.*}}[0, 0, 5]
// CHECK-NEXT: llvm.load %{{.*}} : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(!sD [0]), Block>, StorageBuffer>
  spirv.func @read() -> f32 "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!sD [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %c3 = spirv.Constant 3 : si32
    %ac = spirv.AccessChain %addr[%c0, %c0, %c3] : !spirv.ptr<!spirv.struct<(!sD [0]), Block>, StorageBuffer>, si32, si32, si32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
  spirv.EntryPoint "GLCompute" @read
  spirv.ExecutionMode @read "LocalSize", 1, 1, 1
}
