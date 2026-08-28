// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a mesh entry point's output topology, maximum output vertex
// count and maximum output primitive count execution modes (roadmap H6a)
// survive the conversion as the `feme.mesh.*` passthrough function
// attributes `feme::graphics::getMeshState` (Graphics/Mesh.h) reads back,
// alongside its workgroup size as the existing `hlsl.numthreads` attribute.

// CHECK-NOT: __spv__
// CHECK: llvm.func @mesh_entry()
// CHECK-SAME: ["hlsl.numthreads", "1,1,1"]
// CHECK-SAME: ["feme.mesh.output_topology", "triangles"]
// CHECK-SAME: ["feme.mesh.max_output_vertices", "64"]
// CHECK-SAME: ["feme.mesh.max_output_primitives", "126"]
// CHECK-NOT: feme.geometry
// CHECK-NOT: feme.tessellation
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @mesh_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @mesh_entry
  spirv.ExecutionMode @mesh_entry "LocalSize", 1, 1, 1
  spirv.ExecutionMode @mesh_entry "OutputTrianglesEXT"
  spirv.ExecutionMode @mesh_entry "OutputVertices", 64
  spirv.ExecutionMode @mesh_entry "OutputPrimitivesEXT", 126

}

// -----

// Every mesh output topology execution mode maps onto its own
// `feme.mesh.output_topology` spelling, not just `OutputTrianglesEXT`.

// CHECK: llvm.func @mesh_points_entry()
// CHECK-SAME: ["feme.mesh.output_topology", "points"]
// CHECK-SAME: ["feme.mesh.max_output_vertices", "32"]
// CHECK-SAME: ["feme.mesh.max_output_primitives", "32"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @mesh_points_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @mesh_points_entry
  spirv.ExecutionMode @mesh_points_entry "LocalSize", 32, 1, 1
  spirv.ExecutionMode @mesh_points_entry "OutputPoints"
  spirv.ExecutionMode @mesh_points_entry "OutputVertices", 32
  spirv.ExecutionMode @mesh_points_entry "OutputPrimitivesEXT", 32
}

// -----

// CHECK: llvm.func @mesh_lines_entry()
// CHECK-SAME: ["feme.mesh.output_topology", "lines"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @mesh_lines_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "MeshEXT" @mesh_lines_entry
  spirv.ExecutionMode @mesh_lines_entry "LocalSize", 32, 1, 1
  spirv.ExecutionMode @mesh_lines_entry "OutputLinesEXT"
  spirv.ExecutionMode @mesh_lines_entry "OutputVertices", 64
  spirv.ExecutionMode @mesh_lines_entry "OutputPrimitivesEXT", 32
}

// -----

// A mesh entry point's `OutputPoints`/`OutputVertices` execution modes
// (roadmap H6a) share their SPIR-V enumerant value with a geometry entry's
// own point-output mode and, respectively, a hull entry's output control
// point count / a geometry entry's maximum emitted vertex count (see
// spirv-to-llvm-geometry-execution-modes.mlir and
// spirv-to-llvm-tessellation-execution-modes.mlir for those sides of the
// same enumerants): this entry point's own `Stage` (its `spirv.EntryPoint`
// "MeshEXT" declaration) must route both onto the `feme.mesh.*` attributes
// rather than `feme.geometry.*`/`feme.tessellation.*`, which the
// `mesh_points_entry` case above already confirms via its own `CHECK-NOT`
// on `mesh_entry`.

// A task entry point declares no shape beyond its workgroup size: no
// `feme.mesh.*`/`feme.geometry.*` attribute appears on it at all.

// CHECK: llvm.func @task_entry()
// CHECK-SAME: ["hlsl.numthreads", "1,1,1"]
// CHECK-NOT: feme.mesh
// CHECK-NOT: feme.geometry
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @task_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TaskEXT" @task_entry
  spirv.ExecutionMode @task_entry "LocalSize", 1, 1, 1
}
