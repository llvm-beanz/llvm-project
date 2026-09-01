// A `void main()` that samples a bound 2D sampled image through a separate
// bound sampler at uv (0.75, 0.75) with an explicit LOD of 0, and writes the
// four resulting components to a `StorageBuffer` -- the same shape
// feme/unittests/Vulkan/CommandBufferTest.cpp's SampledImageDispatchTest
// exercises directly against feme::vulkan's own entry points. Used by
// feme/test/Vulkan/sampled-image-loader-smoke.test, serialized ahead of time
// with `feme-translate --serialize-spirv`.
//
// An *explicit* LOD is used rather than an implicit one because a compute
// shader has no fragment derivatives to compute one from.
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.GlobalVariable @out bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @main() -> () "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %image = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %1 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %sampler = spirv.Load "UniformConstant" %1 : !spirv.sampler
    %si = spirv.SampledImage %image, %sampler : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %uv = spirv.Constant dense<[7.500000e-01, 7.500000e-01]> : vector<2xf32>
    %lod = spirv.Constant 0.000000e+00 : f32
    %texel = spirv.ImageSampleExplicitLod %si, %uv ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    %2 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %c2 = spirv.Constant 2 : i32
    %c3 = spirv.Constant 3 : i32
    %r = spirv.CompositeExtract %texel[0 : i32] : vector<4xf32>
    %g = spirv.CompositeExtract %texel[1 : i32] : vector<4xf32>
    %b = spirv.CompositeExtract %texel[2 : i32] : vector<4xf32>
    %a = spirv.CompositeExtract %texel[3 : i32] : vector<4xf32>
    %ac0 = spirv.AccessChain %2[%c0, %c0] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac0, %r : f32
    %ac1 = spirv.AccessChain %2[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac1, %g : f32
    %ac2 = spirv.AccessChain %2[%c0, %c2] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac2, %b : f32
    %ac3 = spirv.AccessChain %2[%c0, %c3] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    spirv.Store "StorageBuffer" %ac3, %a : f32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main, @img, @samp, @out
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
