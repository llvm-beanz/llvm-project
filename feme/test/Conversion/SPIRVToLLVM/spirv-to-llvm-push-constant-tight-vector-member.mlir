// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H124o regression test: a push-constant (scalar/`std430`-layout,
// tightly packed, no implicit vector padding) struct `{uint3 a; uint b;}`
// needs no member reordering at all (its declared offsets, `[0, 12]`,
// already ascend naturally) -- so `OffsetStructMemberReorderAccessChainPattern`
// used to decline entirely for it (its own `NeedsRemap` gate was false),
// leaving MLIR's generic, tight-vector-unaware `AccessChainPattern` to
// build the access chain instead. `a`'s own `vector<3xi32>` member
// converts to `getTightVectorArrayType`'s marker-struct wrapper
// (`!llvm.struct<"feme.tight_vector", (array<3 x i32>)>`), since a
// natural (16-byte, ABI-rounded) LLVM `vector<3xi32>` would overshoot the
// 12 bytes this struct's own tight layout reserves for it -- one level of
// nesting deeper than the generic pattern's forwarded indices account
// for, producing an out-of-bounds struct index (component index 1 or 2
// against a 1-member wrapper struct) for `a.y`/`a.z`.
//
// The fix broadens `OffsetStructMemberReorderAccessChainPattern`'s own
// gate to also proceed whenever an access reaches past the member
// selector into an offset-decorated struct (not just when reordering is
// needed), and `remapNestedStructMemberIndices` now inserts the marker
// struct's own extra `0` index whenever the *real* converted field type
// (via `getStructMemberPhysicalFieldType`, not a standalone reconversion
// of the vector type on its own) is that wrapper.
// CHECK: llvm.mlir.global external constant @pc() {addr_space = 13 : i32} : !llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>, i32)>
// CHECK-LABEL: llvm.func @read_vector_component
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, 0, 0, %{{.*}}]
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<13> -> i32
// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[BASE2:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD2:.*]] = llvm.getelementptr %[[BASE2]][%{{.*}}, 1]
// CHECK: llvm.load %[[FIELD2]] : !llvm.ptr<13> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<type.PushConstant.S, (vector<3xi32> [0], i32 [12]), Block>, PushConstant>
  spirv.func @read_vector_component(%idx : i32) -> i32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<type.PushConstant.S, (vector<3xi32> [0], i32 [12]), Block>, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<type.PushConstant.S, (vector<3xi32> [0], i32 [12]), Block>, PushConstant>, i32, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : i32
    spirv.ReturnValue %v : i32
  }
  spirv.func @read_scalar() -> i32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<type.PushConstant.S, (vector<3xi32> [0], i32 [12]), Block>, PushConstant>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<type.PushConstant.S, (vector<3xi32> [0], i32 [12]), Block>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : i32
    spirv.ReturnValue %v : i32
  }
}

// -----

// The sibling shape (`Feature/PushConstant/padding.test`): the vector
// member comes *second*, immediately preceded by a scalar -- exercises
// the same tight-vector fixup with `MemberIndexPos` selecting a
// non-first member, and no trailing member after the vector to require
// any further remapping.
// CHECK: llvm.mlir.global external constant @pc2() {addr_space = 13 : i32} : !llvm.struct<packed (f32, struct<"feme.tight_vector", (array<3 x f32>)>)>
// CHECK-LABEL: llvm.func @read_second_vector_component
// CHECK: %[[BASE3:.*]] = llvm.mlir.addressof @pc2 : !llvm.ptr<13>
// CHECK: %[[FIELD3:.*]] = llvm.getelementptr %[[BASE3]][%{{.*}}, 1, 0, %{{.*}}]
// CHECK: llvm.load %[[FIELD3]] : !llvm.ptr<13> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc2 : !spirv.ptr<!spirv.struct<type.PushConstant.T, (f32 [0], vector<3xf32> [4]), Block>, PushConstant>
  spirv.func @read_second_vector_component(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @pc2 : !spirv.ptr<!spirv.struct<type.PushConstant.T, (f32 [0], vector<3xf32> [4]), Block>, PushConstant>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %idx] : !spirv.ptr<!spirv.struct<type.PushConstant.T, (f32 [0], vector<3xf32> [4]), Block>, PushConstant>, i32, i32 -> !spirv.ptr<f32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
