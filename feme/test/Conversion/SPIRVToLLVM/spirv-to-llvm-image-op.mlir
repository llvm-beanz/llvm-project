// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.Image` -- extracting the plain image handle back out
// of a combined `!spirv.sampled_image` value, the shape a combined
// texture/sampler ("combined image sampler") descriptor's own
// `spirv.ImageFetch` needs (unlike an actual sample, it takes a plain image
// handle, not a sampled one) -- becomes an `llvm.extractvalue` reading
// field 0 of the very `!llvm.struct<(ImageHandle, SamplerHandle)>`
// `spirv.SampledImage` converts to (see `spirv-to-llvm-sampling.mlir`), the
// inverse of that conversion's own `llvm.insertvalue` pair. Roadmap H29h:
// found via a real
// `dEQP-VK.pipeline.pipeline_library.graphics_library.
// independent_sets_random.*` re-run, whose combined-image-sampler
// descriptors take exactly this path.

// CHECK-LABEL: llvm.func @fetch_from_combined
// CHECK: %[[IMG_HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.Image"
// CHECK: %[[SAMP_HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.Sampler">
// CHECK: %[[PAIR0:.*]] = llvm.mlir.poison : !llvm.struct<(target<"spirv.Image"{{.*}}>, target<"spirv.Sampler">)>
// CHECK: %[[PAIR1:.*]] = llvm.insertvalue %[[IMG_HANDLE]], %[[PAIR0]][0]
// CHECK: %[[PAIR2:.*]] = llvm.insertvalue %[[SAMP_HANDLE]], %[[PAIR1]][1]
// CHECK: %[[IMG:.*]] = llvm.extractvalue %[[PAIR2]][0]
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[IMG]], %{{.*}})
// CHECK: llvm.load %[[PTR]] : !llvm.ptr -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @fetch_from_combined(%coord : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.Image %4 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %6 = spirv.ImageFetch %5, %coord : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %6 : vector<4xf32>
  }
}
