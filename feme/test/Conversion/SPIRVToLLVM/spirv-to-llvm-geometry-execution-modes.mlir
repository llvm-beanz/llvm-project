// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a geometry entry point's input/output primitive class,
// invocation count and maximum output vertex count execution modes
// (roadmap H5a) survive the conversion as the `feme.geometry.*`
// passthrough function attributes `feme::graphics::getGeometryState`
// (Graphics/Geometry.h) reads back.

// CHECK-NOT: __spv__
// CHECK: llvm.func @geom_entry()
// CHECK-SAME: ["feme.geometry.input_primitive", "triangles"]
// CHECK-SAME: ["feme.geometry.output_primitive", "triangle_strip"]
// CHECK-SAME: ["feme.geometry.max_output_vertices", "3"]
// CHECK-SAME: ["feme.geometry.invocations", "1"]
// CHECK-NOT: feme.tessellation
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.func @geom_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @geom_entry
  spirv.ExecutionMode @geom_entry "Triangles"
  spirv.ExecutionMode @geom_entry "OutputTriangleStrip"
  spirv.ExecutionMode @geom_entry "OutputVertices", 3
}

// -----

// An explicit `Invocations` execution mode (GLSL's `invocations` layout
// qualifier) overrides the default of 1.

// CHECK: llvm.func @geom_instanced_entry()
// CHECK-SAME: ["feme.geometry.input_primitive", "points"]
// CHECK-SAME: ["feme.geometry.output_primitive", "points"]
// CHECK-SAME: ["feme.geometry.max_output_vertices", "1"]
// CHECK-SAME: ["feme.geometry.invocations", "4"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.func @geom_instanced_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @geom_instanced_entry
  spirv.ExecutionMode @geom_instanced_entry "InputPoints"
  spirv.ExecutionMode @geom_instanced_entry "OutputPoints"
  spirv.ExecutionMode @geom_instanced_entry "OutputVertices", 1
  spirv.ExecutionMode @geom_instanced_entry "Invocations", 4
}

// -----

// Every input primitive execution mode maps onto its own
// `feme.geometry.input_primitive` spelling, not just `Triangles`.

// CHECK: llvm.func @adjacency_entry()
// CHECK-SAME: ["feme.geometry.input_primitive", "triangles_adjacency"]
// CHECK-SAME: ["feme.geometry.output_primitive", "line_strip"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.func @adjacency_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @adjacency_entry
  spirv.ExecutionMode @adjacency_entry "InputTrianglesAdjacency"
  spirv.ExecutionMode @adjacency_entry "OutputLineStrip"
  spirv.ExecutionMode @adjacency_entry "OutputVertices", 6
}

// -----

// A geometry entry point's `Triangles`/`OutputVertices` execution modes
// (roadmap H5a) share their SPIR-V enumerant value with a tessellation-
// evaluation entry's domain and a tessellation-control entry's output
// control point count respectively (see
// spirv-to-llvm-tessellation-execution-modes.mlir for the tessellation
// side of the same enumerant): this entry point's own `Stage` (its
// `spirv.EntryPoint` "Geometry" declaration) must route both onto the
// `feme.geometry.*` attributes rather than `feme.tessellation.*`, which
// the `geom_entry` case above already confirms via its own `CHECK-NOT`.
