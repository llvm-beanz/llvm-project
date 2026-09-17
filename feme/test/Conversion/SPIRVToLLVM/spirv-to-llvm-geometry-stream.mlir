// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H5e-a: `spirv.EmitVertex`/`spirv.EndPrimitive` -- a real GLSL
// geometry shader's `EmitVertex()`/`EndPrimitive()` -- convert directly into
// calls to the `feme.stage.stream.emit`/`feme.stage.stream.cut`
// `feme::StageOpKind` intrinsics `feme::cpu::lowerGeometryStreamEmit`/
// `lowerGeometryStreamCut` (GeometryWrapper.cpp, built under roadmap G5)
// already know how to lower into a `GeometryStreamBuilder::emit`/`cut`,
// rather than being left unlegalized. Both ops always name output stream 0,
// the only stream `GeometryState`/`FemeGeometryArgs` support today.
//
// Roadmap H39: `spirv.EmitStreamVertex`/`spirv.EndStreamPrimitive` -- the
// real multi-stream `EmitStreamVertex(stream)`/`EndStreamPrimitive(stream)`
// builtins, needing MLIR's SPIR-V dialect to gain the ops themselves first
// -- convert the same way, threading the op's own real `stream` operand
// through instead of a hardcoded `0`.

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

// -----

// Roadmap H39: `spirv.EmitStreamVertex`/`spirv.EndStreamPrimitive` -- the
// real multi-stream `EmitStreamVertex(stream)`/`EndStreamPrimitive(stream)`
// GLSL geometry shader builtins -- convert the same way, except the stream
// operand is the op's own real `stream` SSA value (always an `OpConstant`
// per the SPIR-V spec) rather than a hardcoded `0`.

// CHECK-LABEL: llvm.func @emit_and_cut_stream
// CHECK: %[[STREAM:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: llvm.call @feme.stage.stream.emit(%[[STREAM]]) : (i32) -> ()
// CHECK: llvm.call @feme.stage.stream.cut(%[[STREAM]]) : (i32) -> ()
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [GeometryStreams], []> {
  spirv.func @emit_and_cut_stream() -> () "None" {
    %0 = spirv.Constant 1 : i32
    spirv.EmitStreamVertex %0 : i32
    spirv.EndStreamPrimitive %0 : i32
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @emit_and_cut_stream
  spirv.ExecutionMode @emit_and_cut_stream "Triangles"
  spirv.ExecutionMode @emit_and_cut_stream "OutputTriangleStrip"
  spirv.ExecutionMode @emit_and_cut_stream "OutputVertices", 3
}
