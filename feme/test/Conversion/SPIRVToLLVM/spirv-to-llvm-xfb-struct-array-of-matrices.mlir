// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// A transform feedback `Block` output whose member is an array of offset-less
// structs, each holding a vector and an array of matrices (the shape
// `dEQP-VK.transform_feedback.fuzz.single_struct_array.*` emits), must convert
// with each element body tightly packed: the enclosing block's declared
// offsets (0, 4, 100) reserve exactly 48 bytes per element, with no
// vector-ABI alignment padding.

// CHECK: llvm.mlir.global external @xfb_out() {{.*}} : !llvm.struct<packed (i32, array<2 x struct<(struct<"feme.tight_vector.i32x3", (array<3 x i32>)>, array<2 x array<2 x struct<"feme.tight_vector.f32x2", (array<2 x f32>)>>>, f32)>>, struct<"feme.tight_vector.f32x4", (array<4 x f32>)>)>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @xfb_out {location = 0 : i32, xfb_buffer = 0 : i32, xfb_stride = 116 : i32} : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.array<2 x !spirv.struct<(vector<3xsi32>, !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>>, f32)>> [4], vector<4xf32> [100]), Block>, Output>
}

// -----

// An access chain reaching a scalar inside the array of matrices must follow
// the tightened embedded element (inner member 1, not a physical index
// re-derived from the struct in isolation) and cross the tight column-vector
// marker struct with an extra `0` index.

// CHECK-LABEL: llvm.func @store_component
// CHECK: llvm.getelementptr %{{.*}}[%{{.*}}, 1, %{{.*}}, 1, %{{.*}}, %{{.*}}, 0, %{{.*}}]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @xfb_out {location = 0 : i32, xfb_buffer = 0 : i32, xfb_stride = 116 : i32} : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.array<2 x !spirv.struct<(vector<3xsi32>, !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>>, f32)>> [4], vector<4xf32> [100]), Block>, Output>
  spirv.func @store_component() -> () "None" {
    %v = spirv.mlir.addressof @xfb_out : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.array<2 x !spirv.struct<(vector<3xsi32>, !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>>, f32)>> [4], vector<4xf32> [100]), Block>, Output>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %f = spirv.Constant 1.0 : f32
    %p = spirv.AccessChain %v[%c1, %c1, %c1, %c0, %c1, %c0] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.array<2 x !spirv.struct<(vector<3xsi32>, !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>>, f32)>> [4], vector<4xf32> [100]), Block>, Output>, i32, i32, i32, i32, i32, i32 -> !spirv.ptr<f32, Output>
    spirv.Store "Output" %p, %f : f32
    spirv.Return
  }
}
