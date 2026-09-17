// RUN: mlir-opt %s | FileCheck %s

//===----------------------------------------------------------------------===//
// spirv.EmitVertex
//===----------------------------------------------------------------------===//

func.func @emit_vertex() {
  // CHECK: spirv.EmitVertex
  spirv.EmitVertex
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.EndPrimitive
//===----------------------------------------------------------------------===//

func.func @end_primitive() {
  // CHECK: spirv.EndPrimitive
  spirv.EndPrimitive
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.EmitStreamVertex
//===----------------------------------------------------------------------===//

func.func @emit_stream_vertex() {
  %0 = spirv.Constant 0 : i32
  // CHECK: spirv.EmitStreamVertex %{{.*}} : i32
  spirv.EmitStreamVertex %0 : i32
  spirv.Return
}

//===----------------------------------------------------------------------===//
// spirv.EndStreamPrimitive
//===----------------------------------------------------------------------===//

func.func @end_stream_primitive() {
  %0 = spirv.Constant 0 : i32
  // CHECK: spirv.EndStreamPrimitive %{{.*}} : i32
  spirv.EndStreamPrimitive %0 : i32
  spirv.Return
}
