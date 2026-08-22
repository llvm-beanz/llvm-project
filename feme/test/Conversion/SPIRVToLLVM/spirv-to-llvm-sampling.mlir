// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that sampling an image -- `spirv.SampledImage` combining an image
// and sampler handle, followed by `spirv.ImageSampleImplicitLod` -- becomes
// the `llvm.spv.resource.sample` call LLVM's SPIRV backend selects
// `OpSampledImage`+`OpImageSampleImplicitLod` from (see
// `llvm/test/CodeGen/SPIRV/hlsl-resources/Sample.ll`): the two handles are
// carried as a struct through the dialect conversion (there is no combined
// handle value at the LLVM IR level, unlike MLIR's own conversion, which
// targets the SPIR-V runner) and unpacked again where the intrinsic needs
// them as separate arguments.

// CHECK-LABEL: llvm.func @sample
// CHECK: %[[IMG_HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.Image"
// CHECK: %[[SAMP_HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.Sampler">
// CHECK: %[[PAIR0:.*]] = llvm.mlir.poison : !llvm.struct<(target<"spirv.Image"{{.*}}>, target<"spirv.Sampler">)>
// CHECK: %[[PAIR1:.*]] = llvm.insertvalue %[[IMG_HANDLE]], %[[PAIR0]][0]
// CHECK: %[[PAIR2:.*]] = llvm.insertvalue %[[SAMP_HANDLE]], %[[PAIR1]][1]
// CHECK: %[[IMG:.*]] = llvm.extractvalue %[[PAIR2]][0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %[[PAIR2]][1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.sample"(%[[IMG]], %[[SAMP]], %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample(%coord : vector<2xf32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleImplicitLod %4, %coord : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32> -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// `spirv.ImageFetch` -- a direct texel fetch from a (non-`RWTexture`)
// `Texture` resource, as opposed to `spirv.ImageRead`'s load from a UAV --
// converts exactly like `spirv.ImageRead` does: LLVM's SPIRV backend itself
// picks `OpImageFetch` vs `OpImageRead` from the handle's underlying image
// type (whether its `Sampled` operand is 1), not from which intrinsic
// produced the load (see `generateImageReadOrFetch` in
// `llvm/lib/Target/SPIRV/SPIRVInstructionSelector.cpp`), so no separate
// intrinsic is needed for it.

// CHECK-LABEL: llvm.func @fetch
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[PTR]] : !llvm.ptr -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.func @fetch(%coord : vector<2xsi32>) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.ImageFetch %1, %coord : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

// -----

// `spirv.ImageSampleExplicitLod` with a lone `Lod` image operand converts to
// `llvm.spv.resource.samplelevel`, threading the explicit LOD value through
// instead of defaulting it the way `ImageSampleImplicitLodPattern` does
// (roadmap R30).

// CHECK-LABEL: llvm.func @sample_level
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplelevel"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_level(%coord : vector<2xf32>, %lod : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Lod"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// SPIR-V 1.6's `Nontemporal` image-operand bit, combined with `Lod`, still
// converts to `llvm.spv.resource.samplelevel` the same way (roadmap E17,
// see the `@fetch_level_nontemporal` case below for the fetch analogue).

// CHECK-LABEL: llvm.func @sample_level_nontemporal
// CHECK: %[[IMG:.*]] = llvm.extractvalue %{{.*}}[0]
// CHECK: %[[SAMP:.*]] = llvm.extractvalue %{{.*}}[1]
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.samplelevel"(%[[IMG]], %[[SAMP]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @img bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.GlobalVariable @samp bind(0, 1) : !spirv.ptr<!spirv.sampler, UniformConstant>
  spirv.func @sample_level_nontemporal(%coord : vector<2xf32>, %lod : f32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @img : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.mlir.addressof @samp : !spirv.ptr<!spirv.sampler, UniformConstant>
    %3 = spirv.Load "UniformConstant" %2 : !spirv.sampler
    %4 = spirv.SampledImage %1, %3 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %5 = spirv.ImageSampleExplicitLod %4, %coord ["Lod|Nontemporal"], %lod : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.ReturnValue %5 : vector<4xf32>
  }
}

// -----

// `spirv.ImageFetch` with a lone `Lod` image operand -- what `dxc` always
// emits for `Texture2D<T>::Load`, even a literal 0 mip -- converts to
// `llvm.spv.resource.load.level`, threading the explicit mip level through
// instead of the plain `llvm.load` the unmodified `@fetch` case above uses.

// CHECK-LABEL: llvm.func @fetch_level
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.load.level"(%[[HANDLE]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.func @fetch_level(%coord : vector<2xsi32>, %lod : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.ImageFetch %1, %coord ["Lod"], %lod : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32>, si32 -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

// -----

// SPIR-V 1.6's `Nontemporal` image-operand bit, combined with `Lod`, still
// converts to `llvm.spv.resource.load.level` -- the cache hint has no
// correctness effect and this converter has no caching model to honor it
// with, so it is accepted and discarded rather than rejected the way any
// other unmodeled modifier bit would be (roadmap E17).

// CHECK-LABEL: llvm.func @fetch_level_nontemporal
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[OFFSET:.*]] = llvm.mlir.constant(dense<0> : vector<2xi32>) : vector<2xi32>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.load.level"(%[[HANDLE]], %{{.*}}, %{{.*}}, %[[OFFSET]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @tex bind(0, 0) : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
  spirv.func @fetch_level_nontemporal(%coord : vector<2xsi32>, %lod : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @tex : !spirv.ptr<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>
    %2 = spirv.ImageFetch %1, %coord ["Lod|Nontemporal"], %lod : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32>, si32 -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}
