// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H101n regression test: an "array of block instances" whose per-
// instance struct's own single real member has a nonzero declared offset --
// GLSL's own `layout(xfb_offset = N) out Block { T member; } block[3];`
// syntax, found in a real
// `dEQP-VK.transform_feedback.fuzz.random_geometry.nested_structs_instance_arrays`
// shader whose per-instance struct is exactly `(i32 [32])` (32 bytes of
// leading pad ahead of the array's own real 4-byte member). Before the fix,
// `OffsetStructLeadingPadAccessChainPattern` only recognized this shape when
// the leading-pad struct sat directly behind `spirv.AccessChain`'s own base
// pointer (a push-constant block) -- an *array* of such structs left the
// member-selecting index (`spirv.AccessChain`'s own *second* index here, not
// its first) unmodified, so every store landed in the leading pad itself
// (LLVM struct field 0) rather than the real member (field 1), computing the
// wrong byte address for every array element after the first and eventually
// writing out of bounds of the whole variable's own allocated storage.
// CHECK: llvm.mlir.global external @block() {{.*}} : !llvm.array<3 x struct<(array<32 x i8>, i32)>>
// CHECK-LABEL: llvm.func @write_element
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @block : !llvm.ptr<8>
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, %{{.*}}, 1]
// CHECK: llvm.store %{{.*}}, %[[FIELD]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.GlobalVariable @block : !spirv.ptr<!spirv.array<3 x !spirv.struct<(si32 [32]), Block>>, Output>
  spirv.func @write_element(%idx : si32, %val : si32) -> () "None" {
    %0 = spirv.mlir.addressof @block : !spirv.ptr<!spirv.array<3 x !spirv.struct<(si32 [32]), Block>>, Output>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%idx, %c0] : !spirv.ptr<!spirv.array<3 x !spirv.struct<(si32 [32]), Block>>, Output>, si32, si32 -> !spirv.ptr<si32, Output>
    spirv.Store "Output" %ac, %val : si32
    spirv.Return
  }
}
