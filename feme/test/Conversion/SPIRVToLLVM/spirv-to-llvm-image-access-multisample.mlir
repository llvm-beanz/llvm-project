// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H19g: a plain (non-arrayed) multisampled 2D storage image
// `OpImageRead`/`OpImageWrite` carries a lone `Sample` image operand;
// unlike `Dim::SubpassData`'s own `Sample` handling
// (`spirv-to-llvm-subpass-load.mlir`), this converts to the ordinary
// `llvm.spv.resource.getpointer` resource-handle path, with `Sample`
// appended as the coordinate vector's own trailing lane -- structurally
// identical to how an arrayed image's own array-layer coordinate is
// already the vector's 3rd lane.

// CHECK-LABEL: llvm.func @read_write_ms
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xi32>
// CHECK-DAG: %[[X:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<2xi32>
// CHECK-DAG: %[[V0:.*]] = llvm.insertelement %[[X]], %[[POISON]][%{{.*}} : i64] : vector<3xi32>
// CHECK-DAG: %[[Y:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<2xi32>
// CHECK-DAG: %[[V1:.*]] = llvm.insertelement %[[Y]], %[[V0]][%{{.*}} : i64] : vector<3xi32>
// CHECK: %[[COORD:.*]] = llvm.insertelement %{{.*}}, %[[V1]][%{{.*}} : i64] : vector<3xi32>
// CHECK: %[[READ_PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[COORD]]) : (!llvm.target<"spirv.Image", f32, 1, 0, 0, 1, 2, 1>, vector<3xi32>) -> !llvm.ptr
// CHECK: %[[TEXEL:.*]] = llvm.load %[[READ_PTR]] : !llvm.ptr -> vector<4xf32>
// CHECK: %[[WRITE_PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}}) : (!llvm.target<"spirv.Image", f32, 1, 0, 0, 1, 2, 1>, vector<3xi32>) -> !llvm.ptr
// CHECK: llvm.store %[[TEXEL]], %[[WRITE_PTR]] : vector<4xf32>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @read_write_ms(%coord : vector<2xi32>, %sample : si32) -> () "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32f>
    %2 = spirv.ImageRead %1, %coord ["Sample"], %sample : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32f>, vector<2xi32>, si32 -> vector<4xf32>
    spirv.ImageWrite %1, %coord, %2 ["Sample"], %sample : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32f>, vector<2xi32>, vector<4xf32>, si32
    spirv.Return
  }
}

// -----

// The integer-channel counterpart of `read_write_ms` above -- confirms the
// `Sample` widening is channel-type-agnostic (both share one pattern).

// CHECK-LABEL: llvm.func @read_write_ms_i32
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"(%{{.*}}, %{{.*}}) : (!llvm.target<"spirv.Image", i32, 1, 0, 0, 1, 2, 21>, vector<3xi32>) -> !llvm.ptr
// CHECK: llvm.load
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"(%{{.*}}, %{{.*}}) : (!llvm.target<"spirv.Image", i32, 1, 0, 0, 1, 2, 21>, vector<3xi32>) -> !llvm.ptr
// CHECK: llvm.store
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32i>, UniformConstant>
  spirv.func @read_write_ms_i32(%coord : vector<2xi32>, %sample : si32) -> () "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32i>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32i>
    %2 = spirv.ImageRead %1, %coord ["Sample"], %sample : !spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32i>, vector<2xi32>, si32 -> vector<4xi32>
    spirv.ImageWrite %1, %coord, %2 ["Sample"], %sample : !spirv.image<i32, Dim2D, NoDepth, NonArrayed, MultiSampled, NoSampler, Rgba32i>, vector<2xi32>, vector<4xi32>, si32
    spirv.Return
  }
}

// -----

// Roadmap H19m: an *arrayed* multisampled 2D storage image's own
// `OpImageRead`/`OpImageWrite` -- the case `spirv-to-llvm-image-access-
// invalid.mlir` previously asserted was rejected (roadmap H19g's own
// scope limit) now converts the same way `read_write_ms` above does,
// except starting from a 3-wide `(x, y, layer)` coordinate (the array
// layer is already an ordinary coordinate component, unlike `Sample`)
// rather than a 2-wide one, so the widened coordinate ends up 4-wide
// `(x, y, layer, sample)` instead of 3-wide.

// CHECK-LABEL: llvm.func @read_write_arrayed_ms
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<4xi32>
// CHECK-DAG: %[[C0:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<3xi32>
// CHECK-DAG: %[[V0:.*]] = llvm.insertelement %[[C0]], %[[POISON]][%{{.*}} : i64] : vector<4xi32>
// CHECK-DAG: %[[C1:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<3xi32>
// CHECK-DAG: %[[V1:.*]] = llvm.insertelement %[[C1]], %[[V0]][%{{.*}} : i64] : vector<4xi32>
// CHECK-DAG: %[[C2:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i64] : vector<3xi32>
// CHECK-DAG: %[[V2:.*]] = llvm.insertelement %[[C2]], %[[V1]][%{{.*}} : i64] : vector<4xi32>
// CHECK: %[[COORD:.*]] = llvm.insertelement %{{.*}}, %[[V2]][%{{.*}} : i64] : vector<4xi32>
// CHECK: %[[READ_PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[COORD]]) : (!llvm.target<"spirv.Image", f32, 1, 0, 1, 1, 2, 1>, vector<4xi32>) -> !llvm.ptr
// CHECK: %[[TEXEL:.*]] = llvm.load %[[READ_PTR]] : !llvm.ptr -> vector<4xf32>
// CHECK: %[[WRITE_PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}}) : (!llvm.target<"spirv.Image", f32, 1, 0, 1, 1, 2, 1>, vector<4xi32>) -> !llvm.ptr
// CHECK: llvm.store %[[TEXEL]], %[[WRITE_PTR]] : vector<4xf32>, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @read_write_arrayed_ms(%coord : vector<3xi32>, %sample : si32) -> () "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>
    %2 = spirv.ImageRead %1, %coord ["Sample"], %sample : !spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, vector<3xi32>, si32 -> vector<4xf32>
    spirv.ImageWrite %1, %coord, %2 ["Sample"], %sample : !spirv.image<f32, Dim2D, NoDepth, Arrayed, MultiSampled, NoSampler, Rgba32f>, vector<3xi32>, vector<4xf32>, si32
    spirv.Return
  }
}

