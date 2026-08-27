// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H5e-a: `spirv.EmitVertex`/`spirv.EndPrimitive` -- a real GLSL
// geometry shader's `EmitVertex()`/`EndPrimitive()` -- convert directly into
// calls to the `feme.stage.stream.emit`/`feme.stage.stream.cut`
// `feme::StageOpKind` intrinsics `feme::cpu::lowerGeometryStreamEmit`/
// `lowerGeometryStreamCut` (GeometryWrapper.cpp, built under roadmap G5)
// already know how to lower into a `GeometryStreamBuilder::emit`/`cut`,
// rather than being left unlegalized. Both ops always name output stream 0,
// the only stream `GeometryState`/`FemeGeometryArgs` support today.

// CHECK-LABEL: llvm.func @emit_and_cut
// CHECK: %[[STREAM0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call @feme.stage.stream.emit(%[[STREAM0]]) : (i32) -> ()
// CHECK: %[[STREAM1:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call @feme.stage.stream.cut(%[[STREAM1]]) : (i32) -> ()
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.func @emit_and_cut() -> () "None" {
    spirv.EmitVertex
    spirv.EndPrimitive
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @emit_and_cut
  spirv.ExecutionMode @emit_and_cut "Triangles"
  spirv.ExecutionMode @emit_and_cut "OutputTriangleStrip"
  spirv.ExecutionMode @emit_and_cut "OutputVertices", 3
}

// -----

// A geometry shader that emits several vertices per primitive (e.g. a
// full triangle strip) issues one `feme.stage.stream.emit` call per
// `EmitVertex()`, all sharing the one `feme.stage.stream.emit` declaration
// (StreamEmit is not overloaded, so there is exactly one such declaration
// per module regardless of call count).

// CHECK-LABEL: llvm.func @emit_three
// CHECK-COUNT-3: llvm.call @feme.stage.stream.emit({{.*}}) : (i32) -> ()
// CHECK: llvm.call @feme.stage.stream.cut({{.*}}) : (i32) -> ()
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.func @emit_three() -> () "None" {
    spirv.EmitVertex
    spirv.EmitVertex
    spirv.EmitVertex
    spirv.EndPrimitive
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @emit_three
  spirv.ExecutionMode @emit_three "Triangles"
  spirv.ExecutionMode @emit_three "OutputTriangleStrip"
  spirv.ExecutionMode @emit_three "OutputVertices", 3
}
