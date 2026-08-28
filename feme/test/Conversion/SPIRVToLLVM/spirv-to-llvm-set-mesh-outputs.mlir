// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.EXT.SetMeshOutputs` (roadmap H6c-a-a-i) converts into a
// call to `feme.stage.set_mesh_outputs`, the same "route straight into a
// `feme.stage.*` intrinsic" treatment `EmitVertex`/`EndPrimitive` already
// get (see spirv-to-llvm-geometry-stream.mlir), rather than being left
// unconverted (which would fail this pass's full conversion, since no
// generic SPIR-V-to-LLVM pattern handles it).

// CHECK: llvm.func @feme.stage.set_mesh_outputs(i32, i32)
// CHECK-LABEL: llvm.func @mesh_entry
// CHECK: llvm.call @feme.stage.set_mesh_outputs(%{{.*}}, %{{.*}}) : (i32, i32) -> ()
// CHECK-NOT: spirv.EXT.SetMeshOutputs
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @mesh_entry() "None" {
    %vcount = spirv.Constant 3 : i32
    %pcount = spirv.Constant 1 : i32
    spirv.EXT.SetMeshOutputs %vcount, %pcount : i32, i32
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @mesh_entry
  spirv.ExecutionMode @mesh_entry "LocalSize", 1, 1, 1
  spirv.ExecutionMode @mesh_entry "OutputTrianglesEXT"
  spirv.ExecutionMode @mesh_entry "OutputVertices", 64
  spirv.ExecutionMode @mesh_entry "OutputPrimitivesEXT", 126
}
