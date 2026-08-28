// RUN: mlir-opt -split-input-file -convert-spirv-to-llvm -verify-diagnostics %s | FileCheck %s

//===----------------------------------------------------------------------===//
// Array type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @array(!llvm.array<16 x f32>, !llvm.array<32 x vector<4xf32>>)
spirv.func @array(!spirv.array<16 x f32>, !spirv.array< 32 x vector<4xf32> >) "None"

// CHECK-LABEL: @array_with_natural_stride(!llvm.array<16 x f32>)
spirv.func @array_with_natural_stride(!spirv.array<16 x f32, stride=4>) "None"

// A 3-component vector's own compact size (12 bytes for `vector<3xf32>`) is
// smaller than its Vulkan base alignment (16 bytes, the same as a
// 4-component vector's), so the `ArrayStride` every std430/std140-
// conformant SPIR-V producer (glslang included) emits for an array of one
// is 16, not 12 -- still a "natural" stride this converts to a plain LLVM
// array for, since it is the element's own real per-array-slot size.
// CHECK-LABEL: @array_with_natural_vector3_stride(!llvm.array<4 x vector<3xf32>>)
spirv.func @array_with_natural_vector3_stride(!spirv.array<4 x vector<3xf32>, stride=16>) "None"

//===----------------------------------------------------------------------===//
// Image type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @image_1d(!llvm.target<"spirv.Image", f32, 0, 0, 0, 0, 0, 0>)
spirv.func @image_1d(!spirv.image<f32, Dim1D, NoDepth, NonArrayed, SingleSampled, SamplerUnknown, Unknown>) "None"

// CHECK-LABEL: @image_2d_sampled(!llvm.target<"spirv.Image", f32, 1, 0, 0, 0, 1, 0>)
spirv.func @image_2d_sampled(!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>) "None"

// CHECK-LABEL: @image_storage_buffer(!llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>)
spirv.func @image_storage_buffer(!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>) "None"

// CHECK-LABEL: @image_depth_arrayed_multisampled(!llvm.target<"spirv.Image", f16, 3, 1, 1, 1, 0, 0>)
spirv.func @image_depth_arrayed_multisampled(!spirv.image<f16, Cube, IsDepth, Arrayed, MultiSampled, SamplerUnknown, Unknown>) "None"

// CHECK-LABEL: @image_depth_unknown(!llvm.target<"spirv.Image", f32, 4, 2, 0, 0, 0, 0>)
spirv.func @image_depth_unknown(!spirv.image<f32, Rect, DepthUnknown, NonArrayed, SingleSampled, SamplerUnknown, Unknown>) "None"

// CHECK-LABEL: @image_subpass_data(!llvm.target<"spirv.Image", f32, 6, 0, 0, 0, 2, 0>)
spirv.func @image_subpass_data(!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>) "None"

// Signless and unsigned integer sampled types are indistinguishable in LLVM, so
// only signed ones use the dedicated `spirv.SignedImage` name.

// CHECK-LABEL: @image_signless_integer(!llvm.target<"spirv.Image", i32, 5, 0, 0, 0, 2, 24>)
spirv.func @image_signless_integer(!spirv.image<i32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>) "None"

// CHECK-LABEL: @image_unsigned_integer(!llvm.target<"spirv.Image", i32, 5, 0, 0, 0, 2, 33>)
spirv.func @image_unsigned_integer(!spirv.image<ui32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32ui>) "None"

// CHECK-LABEL: @image_signed_integer(!llvm.target<"spirv.SignedImage", i32, 5, 0, 0, 0, 2, 24>)
spirv.func @image_signed_integer(!spirv.image<si32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>) "None"

//===----------------------------------------------------------------------===//
// Pointer type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @pointer_scalar(!llvm.ptr, !llvm.ptr)
spirv.func @pointer_scalar(!spirv.ptr<i1, Uniform>, !spirv.ptr<f32, Private>) "None"

// CHECK-LABEL: @pointer_vector(!llvm.ptr)
spirv.func @pointer_vector(!spirv.ptr<vector<4xi32>, Function>) "None"

//===----------------------------------------------------------------------===//
// Runtime array type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @runtime_array_vector(!llvm.array<0 x vector<4xf32>>)
spirv.func @runtime_array_vector(!spirv.rtarray< vector<4xf32> >) "None"

// CHECK-LABEL: @runtime_array_scalar(!llvm.array<0 x f32>)
spirv.func @runtime_array_scalar(!spirv.rtarray<f32>) "None"

//===----------------------------------------------------------------------===//
// Sampled image type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @sampled_image(!llvm.target<"spirv.SampledImage", f32, 1, 0, 0, 0, 1, 0>)
spirv.func @sampled_image(!spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>) "None"

// CHECK-LABEL: @sampled_image_signed_integer(!llvm.target<"spirv.SampledImage", i32, 1, 0, 0, 0, 1, 0>)
spirv.func @sampled_image_signed_integer(!spirv.sampled_image<!spirv.image<si32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>) "None"

//===----------------------------------------------------------------------===//
// Sampler type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @sampler(!llvm.target<"spirv.Sampler">)
spirv.func @sampler(!spirv.sampler) "None"

//===----------------------------------------------------------------------===//
// Struct type
//===----------------------------------------------------------------------===//

// CHECK-LABEL: @struct(!llvm.struct<packed (f64)>)
spirv.func @struct(!spirv.struct<(f64)>) "None"

// CHECK-LABEL: @struct_nested(!llvm.struct<packed (i32, struct<packed (i64, i32)>)>)
spirv.func @struct_nested(!spirv.struct<(i32, !spirv.struct<(i64, i32)>)>) "None"

// CHECK-LABEL: @struct_with_natural_offset(!llvm.struct<(i8, i32)>)
spirv.func @struct_with_natural_offset(!spirv.struct<(i8[0], i32[4])>) "None"
