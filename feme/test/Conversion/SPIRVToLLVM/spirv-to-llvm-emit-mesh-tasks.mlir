// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.EXT.EmitMeshTasks` (roadmap H6s) converts into a call
// to `feme.stage.emit_mesh_tasks` followed by an `llvm.return` -- the same
// "route straight into a `feme.stage.*` intrinsic" treatment
// `SetMeshOutputsEXTConversionPattern` already gives the mesh stage's own
// no-signature-element op (see spirv-to-llvm-set-mesh-outputs.mlir), except
// this op is a SPIR-V terminator (must be the last instruction of its
// block), so converting it also needs a real LLVM terminator in its place,
// not just erasure.

// CHECK: llvm.func @feme.stage.emit_mesh_tasks(i32, i32, i32)
// CHECK-LABEL: llvm.func @task_entry
// CHECK: llvm.call @feme.stage.emit_mesh_tasks(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> ()
// CHECK-NEXT: llvm.return
// CHECK-NOT: spirv.EXT.EmitMeshTasks
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @task_entry() "None" {
    %x = spirv.Constant 4 : i32
    %y = spirv.Constant 5 : i32
    %z = spirv.Constant 6 : i32
    spirv.EXT.EmitMeshTasks %x, %y, %z : i32, i32, i32
  }
  spirv.EntryPoint "TaskEXT" @task_entry
  spirv.ExecutionMode @task_entry "LocalSize", 1, 1, 1
}

// -----

// Same conversion, but with the op's own optional `Payload` operand
// present (a pointer to a `TaskPayloadWorkgroupEXT`-storage-class global
// variable) -- checks that the operand is simply dropped from the
// resulting call (see `EmitMeshTasksEXTConversionPattern`'s own comment
// on why it carries no value of its own to forward here), not threaded
// through or rejected.

// CHECK-LABEL: llvm.func @task_entry_with_payload
// CHECK: llvm.call @feme.stage.emit_mesh_tasks(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> ()
// CHECK-NEXT: llvm.return
// CHECK-NOT: spirv.EXT.EmitMeshTasks
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.GlobalVariable @payload : !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
  spirv.func @task_entry_with_payload() "None" {
    %x = spirv.Constant 4 : i32
    %y = spirv.Constant 5 : i32
    %z = spirv.Constant 6 : i32
    %p = spirv.mlir.addressof @payload : !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
    spirv.EXT.EmitMeshTasks %x, %y, %z, %p : i32, i32, i32, !spirv.ptr<i32, TaskPayloadWorkgroupEXT>
  }
  spirv.EntryPoint "TaskEXT" @task_entry_with_payload
  spirv.ExecutionMode @task_entry_with_payload "LocalSize", 1, 1, 1
}
