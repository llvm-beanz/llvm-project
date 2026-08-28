// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `TaskPayloadWorkgroupEXT`-storage-class variable -- a task
// entry's bounded payload variable (SPIR-V enum 5402) -- becomes an ordinary
// `llvm.mlir.global` in address space 14, a FeMe-only convention: unlike
// `Workgroup`(3)/`Input`(7)/`Output`(8)/`StorageBuffer`(11)/`Uniform`(12)/
// `PushConstant`(13), LLVM's own SPIR-V backend
// (`storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`) has
// no mapping at all for this storage class (roadmap H6h). A plain
// `spirv.AccessChain`/`spirv.Load`/`spirv.Store` through it converts with
// MLIR's own generic access-chain/load/store patterns, the same way a
// `Workgroup` variable's own access does.
//
// CHECK: llvm.mlir.global external @payload() {addr_space = 14 : i32} : !llvm.array<4 x i32>
// CHECK-LABEL: llvm.func @write_payload
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @payload : !llvm.ptr<14>
// CHECK: %[[ELEM:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, %{{.*}}]
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : i32, !llvm.ptr<14>
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.GlobalVariable @payload : !spirv.ptr<!spirv.array<4 x i32>, TaskPayloadWorkgroupEXT>
  spirv.func @write_payload(%idx : i32, %val : i32) "None" {
    %0 = spirv.mlir.addressof @payload : !spirv.ptr<!spirv.array<4 x i32>, TaskPayloadWorkgroupEXT>
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<4 x i32>, TaskPayloadWorkgroupEXT>, i32 -> !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
    spirv.Store "TaskPayloadWorkgroupEXT" %ac, %val : i32
    spirv.Return
  }
}

// -----

// Reading a payload variable's scalar field -- the mesh-stage side of the
// same variable a task entry writes -- takes the same shape.
// CHECK: llvm.mlir.global external @payload() {addr_space = 14 : i32} : i32
// CHECK-LABEL: llvm.func @read_payload
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @payload : !llvm.ptr<14>
// CHECK: llvm.load %[[BASE]] : !llvm.ptr<14> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.GlobalVariable @payload : !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
  spirv.func @read_payload() -> i32 "None" {
    %0 = spirv.mlir.addressof @payload : !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
    %v = spirv.Load "TaskPayloadWorkgroupEXT" %0 : i32
    spirv.ReturnValue %v : i32
  }
}
