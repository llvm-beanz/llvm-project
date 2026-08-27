// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a tessellation-evaluation entry point's domain/spacing/
// vertex-order execution modes (roadmap H4a) survive the conversion as the
// `feme.tessellation.*` passthrough function attributes
// `feme::graphics::getTessellationState` (Graphics/Tessellation.h) reads
// back, mapping SPIR-V's `Triangles`/`SpacingFractionalOdd`/`VertexOrderCcw`
// onto `feme::graphics::TessellationState`'s `Domain`/`Partitioning`/
// `OutputPrimitive`.

// CHECK-NOT: __spv__
// CHECK: llvm.func @tese_entry()
// CHECK-SAME: ["feme.tessellation.domain", "triangle"]
// CHECK-SAME: ["feme.tessellation.partitioning", "fractional_odd"]
// CHECK-SAME: ["feme.tessellation.output_primitive", "triangle_ccw"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.func @tese_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TessellationEvaluation" @tese_entry
  spirv.ExecutionMode @tese_entry "Triangles"
  spirv.ExecutionMode @tese_entry "SpacingFractionalOdd"
  spirv.ExecutionMode @tese_entry "VertexOrderCcw"
}

// -----

// `PointMode` overrides the domain-implied output primitive with `point`,
// taking priority over the `VertexOrderCw` this entry point also declares
// (SPIR-V allows both together; a point-mode tessellator never reaches the
// vertex-order-dependent triangle winding).

// CHECK: llvm.func @point_mode_entry()
// CHECK-SAME: ["feme.tessellation.domain", "quad"]
// CHECK-SAME: ["feme.tessellation.partitioning", "integer"]
// CHECK-SAME: ["feme.tessellation.output_primitive", "point"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.func @point_mode_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TessellationEvaluation" @point_mode_entry
  spirv.ExecutionMode @point_mode_entry "Quads"
  spirv.ExecutionMode @point_mode_entry "SpacingEqual"
  spirv.ExecutionMode @point_mode_entry "VertexOrderCw"
  spirv.ExecutionMode @point_mode_entry "PointMode"
}

// -----

// An isoline domain declares no `VertexOrderCw`/`VertexOrderCcw`/`PointMode`
// at all (SPIR-V's own rule -- winding order and point mode are meaningless
// for a 1-dimensional domain), so the output primitive defaults to `line`.

// CHECK: llvm.func @isoline_entry()
// CHECK-SAME: ["feme.tessellation.domain", "isoline"]
// CHECK-SAME: ["feme.tessellation.partitioning", "fractional_even"]
// CHECK-SAME: ["feme.tessellation.output_primitive", "line"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.func @isoline_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TessellationEvaluation" @isoline_entry
  spirv.ExecutionMode @isoline_entry "Isolines"
  spirv.ExecutionMode @isoline_entry "SpacingFractionalEven"
}

// -----

// A tessellation-control entry point declares only `OutputVertices` (the
// number of output control points), which becomes
// `feme.tessellation.output_control_points`, and gets no
// `feme.tessellation.domain`/`partitioning`/`output_primitive` at all: those
// are the tessellation-evaluation stage's own execution modes, never the
// control stage's (see this test's `getTessellationState` doc comment).

// CHECK: llvm.func @tesc_entry()
// CHECK-NOT: feme.tessellation.domain
// CHECK-SAME: ["feme.tessellation.output_control_points", "3"]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Tessellation], []> {
  spirv.func @tesc_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "TessellationControl" @tesc_entry
  spirv.ExecutionMode @tesc_entry "OutputVertices", 3
}
