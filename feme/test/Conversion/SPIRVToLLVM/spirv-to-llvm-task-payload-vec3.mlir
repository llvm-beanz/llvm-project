// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap L147) Checks that a `TaskPayloadWorkgroupEXT` struct of two
// `vector<3xi32>` members (matching `dEQP-VK.mesh_shader.ext.builtin.
// num_work_groups_task_and_mesh`'s own `struct TaskData { uvec3 parentId;
// uvec3 parentSize; }`) can be read through a two-level `spirv.AccessChain`
// (struct-member selector then vector-component selector).
//
// This struct carries no `Offset` decorations at all (`TaskPayloadWorkgroupEXT`
// is not a block/buffer storage class), so L104's tight-vector marker-struct
// substitution (`vec3`'s 12-byte packed size needs the 16-byte room its own
// natural LLVM vector conversion would otherwise silently overrun/underrun
// relative to a following sibling member) still applies, but
// `OffsetStructMemberReorderAccessChainPattern`'s own match-decision gate
// used to additionally require `StructTy.hasOffset()` before it would even
// look at the tight-vector case -- stale ever since L104 deliberately
// widened the substitution itself to cover every struct, not just an
// offset-decorated one. That left a non-offset struct's own vector-in-struct
// access chain falling through to a generic pattern with no knowledge of
// the marker-struct wrapper, producing a `getelementptr` one level too
// shallow -- MLIR's own verifier caught this as `'llvm.getelementptr' op
// index 2 indexing a struct is out of bounds`, since the raw component index
// (2, `parentId`'s own `z` component) was applied directly against the
// *outer*, two-member struct instead of hopping through the inner marker
// wrapper first.
//
// CHECK: llvm.mlir.global external @td() {addr_space = 14 : i32} : !llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>, struct<"feme.tight_vector.1", (array<3 x i32>)>)>
// CHECK-LABEL: llvm.func @read_parent_id_z
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @td : !llvm.ptr<14>
// CHECK: %[[GEP:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, 0, 0, %{{.*}}]
// CHECK: llvm.load %[[GEP]] : !llvm.ptr<14> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.GlobalVariable @td : !spirv.ptr<!spirv.struct<(vector<3xi32>, vector<3xi32>)>, TaskPayloadWorkgroupEXT>
  spirv.func @read_parent_id_z() -> i32 "None" {
    %0 = spirv.mlir.addressof @td : !spirv.ptr<!spirv.struct<(vector<3xi32>, vector<3xi32>)>, TaskPayloadWorkgroupEXT>
    %c0 = spirv.Constant 0 : i32
    %c2 = spirv.Constant 2 : i32
    %ac = spirv.AccessChain %0[%c0, %c2] : !spirv.ptr<!spirv.struct<(vector<3xi32>, vector<3xi32>)>, TaskPayloadWorkgroupEXT>, i32, i32 -> !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
    %v = spirv.Load "TaskPayloadWorkgroupEXT" %ac : i32
    spirv.ReturnValue %v : i32
  }
}

// -----

// The struct's *second* member (`parentSize`, physically shifted by the
// first member's own tight-vector wrapper) reads correctly too.
//
// CHECK: llvm.mlir.global external @td2() {addr_space = 14 : i32} : !llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>, struct<"feme.tight_vector.1", (array<3 x i32>)>)>
// CHECK-LABEL: llvm.func @read_parent_size_y
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @td2 : !llvm.ptr<14>
// CHECK: %[[GEP:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, 1, 0, %{{.*}}]
// CHECK: llvm.load %[[GEP]] : !llvm.ptr<14> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.GlobalVariable @td2 : !spirv.ptr<!spirv.struct<(vector<3xi32>, vector<3xi32>)>, TaskPayloadWorkgroupEXT>
  spirv.func @read_parent_size_y() -> i32 "None" {
    %0 = spirv.mlir.addressof @td2 : !spirv.ptr<!spirv.struct<(vector<3xi32>, vector<3xi32>)>, TaskPayloadWorkgroupEXT>
    %c1 = spirv.Constant 1 : i32
    %c1a = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %c1a] : !spirv.ptr<!spirv.struct<(vector<3xi32>, vector<3xi32>)>, TaskPayloadWorkgroupEXT>, i32, i32 -> !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
    %v = spirv.Load "TaskPayloadWorkgroupEXT" %ac : i32
    spirv.ReturnValue %v : i32
  }
}
