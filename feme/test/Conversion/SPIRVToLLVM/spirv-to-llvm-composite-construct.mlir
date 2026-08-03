// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.CompositeConstruct` building a vector converts, which
// MLIR has no pattern for at all: it lowers to an `llvm.mlir.poison` seed
// with one `llvm.insertelement` per lane.

// A splat (e.g. HLSL's `.xxx` swizzle) constructs every lane from the same
// scalar constituent.

// CHECK-LABEL: llvm.func @splat
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[I0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[V0:.*]] = llvm.insertelement %arg0, %[[POISON]][%[[I0]] : i32]
// CHECK: %[[I1:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[V1:.*]] = llvm.insertelement %arg0, %[[V0]][%[[I1]] : i32]
// CHECK: %[[I2:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[V2:.*]] = llvm.insertelement %arg0, %[[V1]][%[[I2]] : i32]
// CHECK: llvm.return %[[V2]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @splat(%x : f32) -> vector<3xf32> "None" {
    %0 = spirv.CompositeConstruct %x, %x, %x : (f32, f32, f32) -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
}

// -----

// A vector constituent supplies a contiguous run of lanes, extracted one at
// a time; a scalar constituent supplies a single lane directly.

// CHECK-LABEL: llvm.func @mixed
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[E0:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[V0:.*]] = llvm.insertelement %[[E0]], %[[POISON]][%{{.*}} : i32]
// CHECK: %[[E1:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[V1:.*]] = llvm.insertelement %[[E1]], %[[V0]][%{{.*}} : i32]
// CHECK: llvm.insertelement %arg1, %[[V1]][%{{.*}} : i32]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @mixed(%v : vector<2xf32>, %z : f32) -> vector<3xf32> "None" {
    %0 = spirv.CompositeConstruct %v, %z : (vector<2xf32>, f32) -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
}
